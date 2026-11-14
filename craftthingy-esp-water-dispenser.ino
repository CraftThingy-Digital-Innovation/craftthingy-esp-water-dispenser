#include <Arduino.h>
#include <CraftThingyCore.h>
#include "WaterDispenserController.h"

#if defined(ESP32)
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#endif

// Hardware Pin Assignment for ESP32:
// - GPIO 18: Button Left (40s Dispense)
// - GPIO 19: Button Middle (Continuous / Cancel / Abort)
// - GPIO 21: Button Right (80s Dispense)
// - GPIO 22: MOSFET Gate Output (Hardware LEDC PWM Soft-Start)
// - GPIO 2:  Status LED Indicator (Onboard LED on ESP32 DevKit)
#if defined(ESP8266)
    static WaterDispenserController dispenserController(3, 2, 1, 0, 2);
#else
    static WaterDispenserController dispenserController(18, 19, 21, 22, 2);
#endif

void setup() {
#if defined(ESP32)
    // Disable ESP32 brownout detector so motor inrush voltage sag does not crash/reboot the MCU
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
#endif

    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println(F("=================================================="));
    Serial.println(F(" CraftThingy - ESP32 Smart Water Dispenser       "));
    Serial.println(F(" OTA-Enabled | 100% Power | Soft-Start Motor      "));
    Serial.println(F("=================================================="));

    // Configure role & GitHub OTA repositories
    ConfigStore::init();
    ConfigStore::setActiveRole("water_dispenser");
    ConfigStore::setGitHubConfig("CraftThingy-Digital-Innovation", 
                                 "craftthingy-esp-core-ota", 
                                 "craftthingy-esp-water-dispenser", "");

    // Launch CraftThingyCore with water dispenser controller attached
    CraftThingyCore.begin(&dispenserController);

    // Wi-Fi Status Diagnostic Print
    Serial.println();
    if (WiFiManager::isConnected()) {
        Serial.printf("[WIFI] Connected to home Wi-Fi! Station IP: %s\n", WiFiManager::getIPAddress().c_str());
    } else if (WiFiManager::isAPMode()) {
        Serial.printf("[WIFI] Setup Hotspot Active: '%s'\n", WiFiManager::getApSSID().c_str());
        Serial.println(F("[WIFI] Connect your phone/laptop to this hotspot and open http://192.168.4.1 to configure Wi-Fi & OTA!"));
    }
}

void loop() {
    CraftThingyCore.loop();
}
