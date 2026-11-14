# CraftThingy ESP32 Smart Drinking Water Dispenser (`craftthingy-esp-water-dispenser`)

[![Hardware](https://img.shields.io/badge/Hardware-ESP32%20%7C%20ESP32--S3%20%7C%20ESP32--C3-blue.svg)]()
[![Role](https://img.shields.io/badge/Role-Water%20Dispenser%20Automation-teal.svg)]()
[![OTA](https://img.shields.io/badge/OTA-Dual%20GitHub%20Releases-orange.svg)]()
[![Power](https://img.shields.io/badge/Power-100%25%20Full%20%2B%20Soft--Start-brightgreen.svg)]()

Smart drinking water dispenser controller firmware engineered for **ESP32** (ESP32-WROOM, ESP32-S3, ESP32-C3) and **ESP8266**. Built on `craftthingy-esp-core-ota` with automatic self-updating GitHub OTA, hardware LEDC soft-start PWM motor protection, and smart pour duration logic.

---

## 🌟 Key Features

- **Smart Pour Modes**:
  - **Left Button (40s)**: 40-second measured cup dispense.
  - **Middle Button**: Continuous pour toggle ON/OFF with a 15-minute emergency safety timeout.
  - **Right Button (80s)**: 80-second large bottle dispense.
- **Smart Duration Switching**:
  - **40s $\to$ 80s**: Extends total pour time to 80s from start without stopping the pump.
  - **80s $\to$ 40s**: Stops/cancels immediately due to impossible time difference.
  - **Same-Button Stop**: Tapping the active button stops dispensing immediately.
- **Hardware Soft-Start**:
  - Smooth 500ms LEDC PWM ramp from 0% to 100% duty cycle prevents inrush stall currents.
  - Runs at **100% full continuous power** once up to speed.
- **50ms Anti-Spark Noise Filter**:
  - Rejects motor brush RF sparks and contact bounce; requires 50ms sustained LOW.
- **Visual Feedback**:
  - Concurrent LED blinks during 500ms soft-start (1x for 40s, 2x for 80s, 3x for Continuous) $\to$ solid ON during pour.
- **CraftThingyCore OTA Integration**:
  - Web UI configuration portal (`CraftThingy-XXXX` failover AP).
  - HMAC-SHA256 authenticated REST API & WebSockets.
  - Automated GitHub OTA firmware updates with safety interlock (never updates while pump is dispensing).

---

## 📌 ESP32 Pinout Mapping

| Function | Default ESP32 Pin | Active State | Description |
| :--- | :--- | :--- | :--- |
| **Left Button (40s)** | **GPIO 18** | Active LOW | Internal pull-up enabled + 50ms filter |
| **Middle Button (Toggle/Cancel)** | **GPIO 19** | Active LOW | Internal pull-up enabled + 50ms filter |
| **Right Button (80s)** | **GPIO 21** | Active LOW | Internal pull-up enabled + 50ms filter |
| **MOSFET Gate** | **GPIO 22** | Active HIGH | LEDC Hardware PWM Channel 0 (1kHz, 8-bit) |
| **Status LED** | **GPIO 2** | Active HIGH | Onboard Blue LED on ESP32 DevKit boards |
| **Power VIN** | **5V / VIN** | 4.5V – 12V | Connect to 5V Step-Up or 12V Boost Output |
| **GND** | **GND** | 0V | Common System Ground |

---

## 🌐 REST API Endpoints (Port 80)

### 1. Trigger Dispense (`POST /api/v1/dispenser/command`)
```json
{
  "command": "dispense_40s"
}
```
Available commands: `dispense_40s`, `dispense_80s`, `dispense_continuous`, `stop`, `status`.

### 2. Status Query (`GET /api/v1/dispenser/status`)
```json
{
  "role": "water_dispenser",
  "running": true,
  "mode": "40s",
  "elapsed_ms": 12450,
  "remaining_ms": 27550,
  "power_pct": 100
}
```

---

## ⚡ Flashing via PlatformIO

```powershell
# Compile & Upload via USB Serial (auto-detects COM port):
C:\Python312\python.exe -m platformio run -e esp32dev --target upload

# Monitor Serial output:
C:\Python312\python.exe -m platformio device monitor -b 115200
```
