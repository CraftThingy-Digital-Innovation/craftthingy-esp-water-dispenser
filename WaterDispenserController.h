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
                             uint8_t ledPin = 2)
        : _btnLeftPin(btnLeftPin), _btnMidPin(btnMidPin), _btnRightPin(btnRightPin),
          _gatePin(gatePin), _ledPin(ledPin),
          _mode(MODE_IDLE), _pumpRunning(false),
          _pumpStartMs(0), _pumpDurationMs(0),
          _pwmChannel(0), _pwmLimited(false),
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

        // Allow EEPROM / ConfigStore overrides if explicitly saved
        int8_t p;
        if ((p = PinManager::getPin("btn_40s")) >= 0) _btnLeftPin = (uint8_t)p;
        if ((p = PinManager::getPin("btn_mid")) >= 0) _btnMidPin = (uint8_t)p;
        if ((p = PinManager::getPin("btn_80s")) >= 0) _btnRightPin = (uint8_t)p;
        if ((p = PinManager::getPin("gate")) >= 0) _gatePin = (uint8_t)p;
        if ((p = PinManager::getPin("led")) >= 0) _ledPin = (uint8_t)p;

        // 2. Configure Button Inputs with Internal Pull-Ups
        pinMode(_btnLeftPin, INPUT_PULLUP);
        pinMode(_btnMidPin, INPUT_PULLUP);
        pinMode(_btnRightPin, INPUT_PULLUP);

        // 3. Configure Status LED
        pinMode(_ledPin, OUTPUT);
        digitalWrite(_ledPin, LOW);

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
    }

    void loop() override {
        uint32_t now = millis();

        // -------------------------------------------------------------
        // 1. Soft-Start Ramp & Concurrent Mode Blinking
        // -------------------------------------------------------------
        if (_softStartActive) {
            uint32_t elapsed = now - _softStartStartMs;
            if (elapsed < 500) {
                // Smooth duty ramp from 10 (4%) up to 255 (100% full power) over 500ms
                uint32_t duty = 10 + (elapsed * (255 - 10)) / 500;
                setGateDuty((uint8_t)duty);

                // Concurrent LED Mode Blinking during 500ms ramp
                bool ledOn = calculateSoftStartLed(elapsed, _softStartBlinks);
                digitalWrite(_ledPin, ledOn ? HIGH : LOW);
            } else {
                // Ramp complete: 100% full power & solid LED
                setGateDuty(255);
                digitalWrite(_ledPin, HIGH);
                _softStartActive = false;
                _ledSolidOn = true;
            }
        }

        // -------------------------------------------------------------
        // 2. Button 1: Left Button (40s Mode / Cancel)
        // -------------------------------------------------------------
        if (checkButtonDebounced(_btnLeftPin, _leftPressStartMs, _leftTriggered)) {
            if (_mode == MODE_40S) {
                // Same button pressed while 40s running -> STOP
                stopPump();
            } else if (_mode == MODE_80S || _mode == MODE_CONTINUOUS) {
                // Switch 80s -> 40s or Continuous -> 40s: cancel/stop immediately
                stopPump();
            } else {
                // Fresh start: 40s
                startPumpFresh(MODE_40S, 40000UL, 1);
            }
        }

        // -------------------------------------------------------------
        // 3. Button 2: Middle Button (Toggle Continuous ON / Cancel / Abort)
        // -------------------------------------------------------------
        if (checkButtonDebounced(_btnMidPin, _midPressStartMs, _midTriggered)) {
            if (_pumpRunning) {
                // Running in ANY mode -> Abort/Cancel immediately!
                stopPump();
            } else {
                // OFF -> Start Continuous Mode (3 blinks)
                startPumpFresh(MODE_CONTINUOUS, 900000UL, 3); // 15 min safety timeout
            }
        }

        // -------------------------------------------------------------
        // 4. Button 3: Right Button (80s Mode / Extension)
        // -------------------------------------------------------------
        if (checkButtonDebounced(_btnRightPin, _rightPressStartMs, _rightTriggered)) {
            if (_mode == MODE_80S) {
                // Same button pressed while 80s running -> STOP
                stopPump();
            } else if (_mode == MODE_40S) {
                // Smart Switch 40s -> 80s: Extend total target to 80s without pausing motor!
                _mode = MODE_80S;
                _pumpDurationMs = 80000UL;
                blinkLedAlert(2, 120, 100);
            } else if (_mode == MODE_CONTINUOUS) {
                stopPump();
            } else {
                // Fresh start: 80s (2 blinks)
                startPumpFresh(MODE_80S, 80000UL, 2);
            }
        }

        // -------------------------------------------------------------
        // 5. Automatic Timer Expiration (Timed Dispense)
        // -------------------------------------------------------------
        if (_pumpRunning && (_mode == MODE_40S || _mode == MODE_80S || _mode == MODE_CONTINUOUS)) {
            if ((now - _pumpStartMs) >= _pumpDurationMs) {
                stopPump();
            }
        }
    }

    // Role name identifier for CraftThingyCore
    const char* getRoleName() const override {
        return "water_dispenser";
    }

    // Application Version
    const char* getAppVersion() const override {
        return "1.0.0";
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
        json += "}}";
        return json;
    }

private:
    uint8_t _btnLeftPin;
    uint8_t _btnMidPin;
    uint8_t _btnRightPin;
    uint8_t _gatePin;
    uint8_t _ledPin;

    DispenserMode _mode;
    bool _pumpRunning;
    uint32_t _pumpStartMs;
    uint32_t _pumpDurationMs;

    uint8_t _pwmChannel;
    bool _pwmLimited;

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

        // 0ms Instant start: initiate soft-start ramp immediately!
        _softStartBlinks = blinks;
        _softStartStartMs = millis();
        _softStartActive = true;
    }

    void stopPump() {
        _mode = MODE_IDLE;
        _pumpRunning = false;
        _softStartActive = false;
        _ledSolidOn = false;

        setGateDuty(0);
        digitalWrite(_ledPin, LOW);
    }

    /**
     * 50ms Anti-Spark Noise Filter:
     * Requires the button pin to be held continuously LOW for at least 50ms
     * to eliminate motor brush electrical sparks. Fires on the leading edge.
     */
    bool checkButtonDebounced(uint8_t pin, uint32_t& pressStartMs, bool& triggered) {
        bool isLow = (digitalRead(pin) == LOW);
        uint32_t now = millis();

        if (isLow) {
            if (pressStartMs == 0) {
                pressStartMs = now; // First detected low edge
            } else if ((now - pressStartMs >= 50) && !triggered) {
                // Held solidly for >= 50ms! Valid human finger press!
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

