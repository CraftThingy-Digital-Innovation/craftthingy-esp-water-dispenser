#pragma once

#include <Arduino.h>
#include <CraftThingyCore.h>

// Operating Modes
enum DispenserMode {
    MODE_IDLE = 0,
    MODE_40S = 1,
    MODE_80S = 2,
    MODE_CONTINUOUS = 3
};

class WaterDispenserController : public IDeviceController {
public:
    WaterDispenserController(uint8_t btnLeftPin = 18, 
                             uint8_t btnMidPin = 19, 
                             uint8_t btnRightPin = 21, 
                             uint8_t gatePin = 22, 
                             uint8_t ledPin = 2,
                             uint8_t boostEnPin = 25)
        : _btnLeftPin(btnLeftPin), _btnMidPin(btnMidPin), _btnRightPin(btnRightPin),
          _gatePin(gatePin), _ledPin(ledPin), _boostEnPin(boostEnPin),
          _mode(MODE_IDLE), _pumpRunning(false),
          _pumpStartMs(0), _pumpDurationMs(0),
          _pwmChannel(0), _pwmLimited(false),
          _maxDuty(255),
          _softStartActive(false), _softStartStartMs(0), _softStartBlinks(1),
          _ledSolidOn(false) {
    }

    void setup() override {
        // 1. Dynamic Pin Registration with CraftThingyCore PinManager
        PinManager::registerRole("btn_40s", "40s Dispense Button", _btnLeftPin, PIN_CAP_INPUT);
        PinManager::registerRole("btn_mid", "Continuous / Cancel Button", _btnMidPin, PIN_CAP_INPUT);
        PinManager::registerRole("btn_80s", "80s Dispense Button", _btnRightPin, PIN_CAP_INPUT);
        PinManager::registerRole("gate", "MOSFET Gate Output", _gatePin, PIN_CAP_OUTPUT);
        PinManager::registerRole("led", "Status LED Indicator", _ledPin, PIN_CAP_OUTPUT);
        if (_boostEnPin != 255) {
            PinManager::registerRole("boost_en", "Boost Converter Enable", _boostEnPin, PIN_CAP_OUTPUT);
        }

        // Allow EEPROM / ConfigStore overrides if explicitly saved
        int8_t p;
        if ((p = PinManager::getPin("btn_40s")) >= 0) _btnLeftPin = (uint8_t)p;
        if ((p = PinManager::getPin("btn_mid")) >= 0) _btnMidPin = (uint8_t)p;
        if ((p = PinManager::getPin("btn_80s")) >= 0) _btnRightPin = (uint8_t)p;
        if ((p = PinManager::getPin("gate")) >= 0) _gatePin = (uint8_t)p;
        if ((p = PinManager::getPin("led")) >= 0) _ledPin = (uint8_t)p;
        if (_boostEnPin != 255 && (p = PinManager::getPin("boost_en")) >= 0) _boostEnPin = (uint8_t)p;

        // 2. Configure Button Inputs with Internal Pull-Ups
        pinMode(_btnLeftPin, INPUT_PULLUP);
        pinMode(_btnMidPin, INPUT_PULLUP);
        pinMode(_btnRightPin, INPUT_PULLUP);

        // 3. Configure Status LED
        pinMode(_ledPin, OUTPUT);
        digitalWrite(_ledPin, LOW);

        // 3.1 Configure Boost Converter Enable Pin (starts in sleep/disabled mode)
        if (_boostEnPin != 255) {
            pinMode(_boostEnPin, OUTPUT);
            digitalWrite(_boostEnPin, LOW);
        }

        // 4. Configure MOSFET Gate (LEDC Hardware PWM on ESP32 / analogWrite on ESP8266)
#if defined(ESP32)
        ledcSetup(_pwmChannel, 1000, 8); // 1 kHz, 8-bit resolution (0-255)
        ledcAttachPin(_gatePin, _pwmChannel);
        ledcWrite(_pwmChannel, 0);
#elif defined(ESP8266)
        pinMode(_gatePin, OUTPUT);
        analogWriteRange(255);
        analogWriteFreq(1000);
        analogWrite(_gatePin, 0);
#endif

        // 5. UART Diagnostics Initialization Banner
        Serial.println();
        Serial.printf("[DISPENSER] Hardware Initialized:\n");
        Serial.printf("  - Left Button (40s)  : GPIO %d (Internal Pull-Up)\n", _btnLeftPin);
        Serial.printf("  - Mid Button (Abort) : GPIO %d (Internal Pull-Up)\n", _btnMidPin);
        Serial.printf("  - Right Button (80s) : GPIO %d (Internal Pull-Up)\n", _btnRightPin);
        Serial.printf("  - MOSFET Gate Output : GPIO %d (LEDC Ch%d, 1kHz, 500ms Soft-Start)\n", _gatePin, _pwmChannel);
        Serial.printf("  - Status Indicator   : GPIO %d\n", _ledPin);
        if (_boostEnPin != 255) {
            Serial.printf("  - Boost Enable (EN)  : GPIO %d (Sleep Standby: LOW)\n", _boostEnPin);
        }
        Serial.println(F("[DISPENSER] Ready. Type '?' in Serial terminal for UART diagnostic commands."));
    }

    void loop() override {
        uint32_t now = millis();

        // -------------------------------------------------------------
        // 0. Interactive UART Console Diagnostics & Commands
        // -------------------------------------------------------------
        if (Serial.available() > 0) {
            char c = (char)Serial.read();
            handleUartCommand(c);
        }

        // -------------------------------------------------------------
        // 0.1 Real-Time Pin Transition Logger
        // -------------------------------------------------------------
        int leftRaw = digitalRead(_btnLeftPin);
        int midRaw = digitalRead(_btnMidPin);
        int rightRaw = digitalRead(_btnRightPin);

        if (leftRaw != _lastLeftRaw) {
            _lastLeftRaw = leftRaw;
            Serial.printf("[PIN-EVENT] Left Button (GPIO %d) -> %s\n", 
                          _btnLeftPin, leftRaw == LOW ? "LOW (Ground / Active)" : "HIGH (Pulled-Up / Idle)");
        }
        if (midRaw != _lastMidRaw) {
            _lastMidRaw = midRaw;
            Serial.printf("[PIN-EVENT] Middle Button (GPIO %d) -> %s\n", 
                          _btnMidPin, midRaw == LOW ? "LOW (Ground / Active)" : "HIGH (Pulled-Up / Idle)");
        }
        if (rightRaw != _lastRightRaw) {
            _lastRightRaw = rightRaw;
            Serial.printf("[PIN-EVENT] Right Button (GPIO %d) -> %s\n", 
                          _btnRightPin, rightRaw == LOW ? "LOW (Ground / Active)" : "HIGH (Pulled-Up / Idle)");
        }

        // -------------------------------------------------------------
        // 1. Soft-Start Ramp & Concurrent Mode Blinking
        // -------------------------------------------------------------
        if (_softStartActive) {
            uint32_t elapsed = now - _softStartStartMs;
            if (elapsed < 500) {
                // Smooth duty ramp from 10 up to _maxDuty (100% full power) over 500ms
                uint32_t duty = 10 + (elapsed * (_maxDuty - 10)) / 500;
                setGateDuty((uint8_t)duty);

                // Concurrent LED Mode Blinking during 500ms ramp
                bool ledOn = calculateSoftStartLed(elapsed, _softStartBlinks);
                digitalWrite(_ledPin, ledOn ? HIGH : LOW);
            } else {
                // Ramp complete: 100% full power & solid LED
                setGateDuty(_maxDuty);
                digitalWrite(_ledPin, HIGH);
                _softStartActive = false;
                _ledSolidOn = true;
                Serial.printf("[MOTOR] Soft-start ramp complete -> Power: %d/255 (100%% Full Power)\n", _maxDuty);
            }
        }

        // -------------------------------------------------------------
        // 2. Button 1: Left Button (40s Mode / Cancel)
        // -------------------------------------------------------------
        if (checkButtonDebounced(_btnLeftPin, _leftPressStartMs, _leftTriggered)) {
            Serial.printf("[BUTTON] Left Button (GPIO %d) triggered!\n", _btnLeftPin);
            if (_mode == MODE_40S) {
                Serial.println(F("[BUTTON] Same button pressed during 40s pour -> ABORT."));
                stopPump();
            } else if (_mode == MODE_80S || _mode == MODE_CONTINUOUS) {
                Serial.println(F("[BUTTON] Cannot switch down from 80s/Continuous to 40s -> STOP."));
                stopPump();
            } else {
                startPumpFresh(MODE_40S, 40000UL, 1);
            }
        }

        // -------------------------------------------------------------
        // 3. Button 2: Middle Button (Toggle Continuous ON / Cancel / Abort)
        // -------------------------------------------------------------
        if (checkButtonDebounced(_btnMidPin, _midPressStartMs, _midTriggered)) {
            Serial.printf("[BUTTON] Middle Button (GPIO %d) triggered!\n", _btnMidPin);
            if (_pumpRunning) {
                Serial.println(F("[BUTTON] Middle Button pressed while pump running -> EMERGENCY ABORT!"));
                stopPump();
            } else {
                startPumpFresh(MODE_CONTINUOUS, 900000UL, 3); // 15 min safety timeout
            }
        }

        // -------------------------------------------------------------
        // 4. Button 3: Right Button (80s Mode / Extension)
        // -------------------------------------------------------------
        if (checkButtonDebounced(_btnRightPin, _rightPressStartMs, _rightTriggered)) {
            Serial.printf("[BUTTON] Right Button (GPIO %d) triggered!\n", _btnRightPin);
            if (_mode == MODE_80S) {
                Serial.println(F("[BUTTON] Same button pressed during 80s pour -> ABORT."));
                stopPump();
            } else if (_mode == MODE_40S) {
                Serial.println(F("[DISPENSER] Smart Switch: 40s -> 80s! Extending total pour duration to 80s."));
                _mode = MODE_80S;
                _pumpDurationMs = 80000UL;
                blinkLedAlert(2, 120, 100);
            } else if (_mode == MODE_CONTINUOUS) {
                Serial.println(F("[BUTTON] Right Button pressed while in Continuous Mode -> STOP."));
                stopPump();
            } else {
                startPumpFresh(MODE_80S, 80000UL, 2);
            }
        }

        // -------------------------------------------------------------
        // 5. Automatic Timer Expiration (Timed Dispense)
        // -------------------------------------------------------------
        if (_pumpRunning && (_mode == MODE_40S || _mode == MODE_80S || _mode == MODE_CONTINUOUS)) {
            uint32_t elapsed = now - _pumpStartMs;
            if (elapsed >= _pumpDurationMs) {
                Serial.printf("[DISPENSER] Target duration reached (%lu ms) -> Automatically stopping pump.\n", _pumpDurationMs);
                stopPump();
            } else {
                // Periodic UART Telemetry Heartbeat every 5000ms while running
                if (now - _lastPeriodicReportMs >= 5000) {
                    _lastPeriodicReportMs = now;
                    uint32_t remainingSec = (_pumpDurationMs - elapsed) / 1000;
                    Serial.printf("[DISPENSER] Pouring (%s): %lu s elapsed, %lu s remaining (Duty: %d/255)\n",
                                  getModeString(), elapsed / 1000, remainingSec, _softStartActive ? 128 : 255);
                }
            }
        }
    }

    // Role name identifier for CraftThingyCore
    const char* getRoleName() const override {
        return "water_dispenser";
    }

    // Application Version
    const char* getAppVersion() const override {
        return "1.1.0";
    }

    // OTA Safety Interlock:
    // Only safe to update firmware if the pump is completely IDLE!
    bool isSafeToUpdate() const override {
        return !_pumpRunning;
    }

    // Emergency fail-safe shutdown before flashing reboot
    void prepareForUpdate() override {
        stopPump();
    }

    // Remote REST & WebSocket Command Handler
    bool handleCommand(const String& cmd, const String& payload, String& response) override {
        if (cmd.equalsIgnoreCase("dispense_40s")) {
            startPumpFresh(MODE_40S, 40000UL, 1);
            response = "{\"status\":\"ok\",\"action\":\"dispense_40s\"}";
            return true;
        } else if (cmd.equalsIgnoreCase("dispense_80s")) {
            startPumpFresh(MODE_80S, 80000UL, 2);
            response = "{\"status\":\"ok\",\"action\":\"dispense_80s\"}";
            return true;
        } else if (cmd.equalsIgnoreCase("dispense_continuous")) {
            startPumpFresh(MODE_CONTINUOUS, 900000UL, 3);
            response = "{\"status\":\"ok\",\"action\":\"dispense_continuous\"}";
            return true;
        } else if (cmd.equalsIgnoreCase("stop") || cmd.equalsIgnoreCase("cancel")) {
            stopPump();
            response = "{\"status\":\"ok\",\"action\":\"stop\"}";
            return true;
        } else if (cmd.equalsIgnoreCase("status")) {
            response = getStatusJson();
            return true;
        }
        return false;
    }

    // Status JSON for Web Portal, App, and Telemetry
    String getStatusJson() override {
        uint32_t now = millis();
        uint32_t elapsed = _pumpRunning ? (now - _pumpStartMs) : 0;
        uint32_t remaining = (_pumpRunning && _pumpDurationMs > elapsed) ? (_pumpDurationMs - elapsed) : 0;

        String json = "{";
        json += "\"role\":\"water_dispenser\",";
        json += "\"running\":" + String(_pumpRunning ? "true" : "false") + ",";
        json += "\"mode\":\"";
        switch (_mode) {
            case MODE_40S: json += "40s"; break;
            case MODE_80S: json += "80s"; break;
            case MODE_CONTINUOUS: json += "continuous"; break;
            default: json += "idle"; break;
        }
        json += "\",";
        json += "\"elapsed_ms\":" + String(elapsed) + ",";
        json += "\"remaining_ms\":" + String(remaining) + ",";
        json += "\"power_pct\":100,";
        json += "\"pins\":{";
        json += "\"btn_40s\":" + String(_btnLeftPin) + ",";
        json += "\"btn_mid\":" + String(_btnMidPin) + ",";
        json += "\"btn_80s\":" + String(_btnRightPin) + ",";
        json += "\"gate\":" + String(_gatePin) + ",";
        json += "\"led\":" + String(_ledPin);
        if (_boostEnPin != 255) {
            json += ",\"boost_en\":" + String(_boostEnPin);
        }
        json += "}}";
        return json;
    }

private:
    uint8_t _btnLeftPin;
    uint8_t _btnMidPin;
    uint8_t _btnRightPin;
    uint8_t _gatePin;
    uint8_t _ledPin;
    uint8_t _boostEnPin;

    DispenserMode _mode;
    bool _pumpRunning;
    uint32_t _pumpStartMs;
    uint32_t _pumpDurationMs;

    uint8_t _pwmChannel;
    bool _pwmLimited;
    uint8_t _maxDuty;

    bool _softStartActive;
    uint32_t _softStartStartMs;
    uint8_t _softStartBlinks;
    bool _ledSolidOn;

    // 50ms Anti-Spark Noise Filter Debounce Timers
    uint32_t _leftPressStartMs = 0;
    bool _leftTriggered = false;

    uint32_t _midPressStartMs = 0;
    bool _midTriggered = false;

    uint32_t _rightPressStartMs = 0;
    bool _rightTriggered = false;

    int _lastLeftRaw = -1;
    int _lastMidRaw = -1;
    int _lastRightRaw = -1;

    uint32_t _lastPeriodicReportMs = 0;

    const char* getModeString() const {
        switch (_mode) {
            case MODE_40S: return "40s Cup Mode";
            case MODE_80S: return "80s Bottle Mode";
            case MODE_CONTINUOUS: return "Continuous Mode";
            default: return "IDLE";
        }
    }

    void handleUartCommand(char c) {
        switch (c) {
            case '1':
                Serial.println(F("\n[UART-CMD] '1' -> Start 40s Dispense"));
                startPumpFresh(MODE_40S, 40000UL, 1);
                break;
            case '2':
                Serial.println(F("\n[UART-CMD] '2' -> Start 80s Dispense"));
                startPumpFresh(MODE_80S, 80000UL, 2);
                break;
            case 'c':
            case 'C':
                Serial.println(F("\n[UART-CMD] 'c' -> Start Continuous Dispense (15m limit)"));
                startPumpFresh(MODE_CONTINUOUS, 900000UL, 3);
                break;
            case 'x':
            case 'X':
            case ' ':
                Serial.println(F("\n[UART-CMD] EMERGENCY STOP / ABORT"));
                stopPump();
                break;
            case 's':
            case 'S':
                Serial.println(F("\n[UART-CMD] Status Query:"));
                Serial.println(getStatusJson());
                break;
            case 'p':
            case 'P':
                Serial.printf("\n[PIN-DIAG] Real-Time Button, Gate & Boost Pin Levels:\n");
                Serial.printf("  - Left Button  (GPIO %d) : %s (digitalRead = %d)\n", 
                              _btnLeftPin, digitalRead(_btnLeftPin) == LOW ? "LOW (Pressed)" : "HIGH (Released)", digitalRead(_btnLeftPin));
                Serial.printf("  - Mid Button   (GPIO %d) : %s (digitalRead = %d)\n", 
                              _btnMidPin, digitalRead(_btnMidPin) == LOW ? "LOW (Pressed)" : "HIGH (Released)", digitalRead(_btnMidPin));
                Serial.printf("  - Right Button (GPIO %d) : %s (digitalRead = %d)\n", 
                              _btnRightPin, digitalRead(_btnRightPin) == LOW ? "LOW (Pressed)" : "HIGH (Released)", digitalRead(_btnRightPin));
                Serial.printf("  - MOSFET Gate  (GPIO %d) : %s (digitalRead = %d)\n", 
                              _gatePin, digitalRead(_gatePin) == HIGH ? "HIGH (MOSFET ON)" : "LOW (MOSFET OFF)", digitalRead(_gatePin));
                if (_boostEnPin != 255) {
                    Serial.printf("  - Boost Enable (GPIO %d) : %s (digitalRead = %d)\n", 
                                  _boostEnPin, digitalRead(_boostEnPin) == HIGH ? "HIGH (Active 12V)" : "LOW (Sleep Standby)", digitalRead(_boostEnPin));
                }
                break;
            case 'w':
            case 'W':
                Serial.println();
                Serial.println(F("=================================================="));
                Serial.println(F(" 🌐 Wi-Fi & OTA Network Status                    "));
                Serial.println(F("=================================================="));
                Serial.printf(" Wi-Fi Connected : %s\n", WiFiManager::isConnected() ? "YES" : "NO");
                if (WiFiManager::isConnected()) {
                    Serial.printf(" Station IP      : %s\n", WiFiManager::getIPAddress().c_str());
                    Serial.printf(" Gateway IP      : %s\n", WiFiManager::getGatewayIP().c_str());
                } else if (WiFiManager::isAPMode()) {
                    Serial.printf(" Setup Hotspot   : %s\n", WiFiManager::getApSSID().c_str());
                    Serial.printf(" Hotspot IP      : %s\n", WiFiManager::getAPIPAddress().c_str());
                    Serial.println(F(" Connect to this hotspot & open http://192.168.4.1 to configure!"));
                }
                Serial.printf(" MAC Address     : %s\n", WiFiManager::getMacAddress().c_str());
                Serial.printf(" GitHub Repo     : CraftThingy-Digital-Innovation/craftthingy-esp-water-dispenser\n");
                Serial.println(F("=================================================="));
                break;
            case '?':
            case 'h':
            case 'H':
                Serial.println();
                Serial.println(F("=================================================="));
                Serial.println(F(" 💧 CraftThingy Dispenser UART Diagnostic Menu    "));
                Serial.println(F("=================================================="));
                Serial.printf(" State   : %s (Running: %s, Duty: %d)\n", 
                              getModeString(), _pumpRunning ? "YES" : "NO", _pumpRunning ? 255 : 0);
                if (_pumpRunning) {
                    uint32_t el = (millis() - _pumpStartMs) / 1000;
                    uint32_t tot = _pumpDurationMs / 1000;
                    Serial.printf(" Pouring : %lu s / %lu s (Remaining: %lu s)\n", el, tot, tot > el ? tot - el : 0);
                }
                Serial.println(F(" Commands:"));
                Serial.println(F("   [1] Dispense 40 seconds"));
                Serial.println(F("   [2] Dispense 80 seconds"));
                Serial.println(F("   [c] Dispense Continuous (15m safety max)"));
                Serial.println(F("   [x] Emergency Stop / Abort"));
                Serial.println(F("   [s] Dump JSON Telemetry"));
                Serial.println(F("   [p] Probe Real-Time Pin Levels"));
                Serial.println(F("   [w] Query Wi-Fi & OTA Network Status"));
                Serial.println(F("   [?] Display this diagnostic menu"));
                Serial.println(F("=================================================="));
                break;
            default:
                break;
        }
    }

    void setGateDuty(uint8_t duty) {
#if defined(ESP32)
        ledcWrite(_pwmChannel, duty);
#elif defined(ESP8266)
        analogWrite(_gatePin, duty);
#endif
    }

    void startPumpFresh(DispenserMode mode, uint32_t durationMs, uint8_t blinks) {
        _mode = mode;
        _pumpDurationMs = durationMs;
        _pumpStartMs = millis();
        _pumpRunning = true;
        _lastPeriodicReportMs = millis();

        // 1. Wake up Boost Converter and allow 50ms for 12V output rail to stabilize
        if (_boostEnPin != 255) {
            digitalWrite(_boostEnPin, HIGH);
            delay(50);
        }

        // 2. Initiate soft-start ramp immediately
        _softStartBlinks = blinks;
        _softStartStartMs = millis();
        _softStartActive = true;

        Serial.printf("[DISPENSER] START POUR -> Mode: %s (%lu ms), Soft-Start Ramp: 500ms%s\n",
                      getModeString(), durationMs, _boostEnPin != 255 ? " (Boost EN: HIGH)" : "");
    }

    void stopPump() {
        if (_pumpRunning) {
            uint32_t elapsed = millis() - _pumpStartMs;
            Serial.printf("[DISPENSER] STOP POUR -> Total runtime: %lu ms (%lu s). Motor Gate OFF (Duty 0).\n",
                          elapsed, elapsed / 1000);
        } else {
            Serial.println(F("[DISPENSER] Motor already OFF."));
        }

        _mode = MODE_IDLE;
        _pumpRunning = false;
        _softStartActive = false;
        _ledSolidOn = false;

        setGateDuty(0);
        digitalWrite(_ledPin, LOW);

        // Put boost converter into sleep mode to eliminate battery standby drain
        if (_boostEnPin != 255) {
            delay(20);
            digitalWrite(_boostEnPin, LOW);
            Serial.println(F("[DISPENSER] Boost Converter disabled -> EN LOW (Sleep Standby)."));
        }
    }

    /**
     * Responsive 20ms Debounce Filter:
     * Fires cleanly when finger presses button for 20ms.
     * Automatically arms again as soon as the button is released.
     */
    bool checkButtonDebounced(uint8_t pin, uint32_t& pressStartMs, bool& triggered) {
        bool isLow = (digitalRead(pin) == LOW);
        uint32_t now = millis();

        if (isLow) {
            if (pressStartMs == 0) {
                pressStartMs = now; // First detected low edge
            } else if ((now - pressStartMs >= 20) && !triggered) {
                // Held for >= 20ms! Valid human finger press!
                triggered = true;
                return true;
            }
        } else {
            // Button released: reset filter for next tap
            pressStartMs = 0;
            triggered = false;
        }
        return false;
    }

    bool calculateSoftStartLed(uint32_t elapsedMs, uint8_t blinks) {
        if (blinks == 1) {
            return (elapsedMs <= 250);
        } else if (blinks == 2) {
            return (elapsedMs <= 125) || (elapsedMs >= 250 && elapsedMs <= 375);
        } else if (blinks == 3) {
            return (elapsedMs <= 83) || (elapsedMs >= 166 && elapsedMs <= 250) || (elapsedMs >= 333 && elapsedMs <= 416);
        }
        return true;
    }

    void blinkLedAlert(uint8_t count, uint16_t onMs, uint16_t offMs) {
        for (uint8_t i = 0; i < count; i++) {
            digitalWrite(_ledPin, HIGH);
            delay(onMs);
            digitalWrite(_ledPin, LOW);
            delay(offMs);
        }
        if (_pumpRunning) {
            digitalWrite(_ledPin, HIGH);
        }
    }
};

