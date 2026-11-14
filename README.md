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

## 📐 Circuit Schematic

```
=========================================================================================================
                           CRAFTTHINGY ESP32 WATER DISPENSER SCHEMATIC
=========================================================================================================

  [ 3.7V - 4.2V Li-Po Battery ]
             │
             ▼
    ┌─────────────────┐
    │  TP4056 + DW01A │  (Preserves short-circuit & under-voltage protection)
    │  BMS Module     │
    │   OUT+     OUT- │
    └───┬──────────┬──┘
        │          │
        │          └──────────────────────────┬─────────────────────────────┬───────────────────────────┐
        │                                     │                             │                           │
        ▼ (3.7V Protected Rail)               ▼                             ▼                           │
   ┌───────────────┐                  ┌───────────────┐                     │                           │
   │ 5V STEP-UP    │                  │ 12V BOOST     │                     │                           │
   │ BOOST MODULE  │                  │ CONVERTER     │                     │                           │
   │  VIN+    VIN- │                  │  VIN+    VIN- │                     │                           │
   │   │        │  │                  │   │        │  │                     │                           │
   │  VOUT+   VOUT-│                  │  VOUT+   VOUT-│                     │                           │
   └───┬────────┬──┘                  └───┬────────┬──┘                     │                           │
       │        │                         │        │                        │                           │
       │        └─────────────────────────┼────────┼────────────────────────┤                           │
       │                                  │        │                        │                           │
       ▼ (5.0V Rail)                      ▼ (12V)  └────────────────────────┼─────────────┐             │
┌───────────────────────────┐      [ 12V PUMP MOTOR ]                       │             │             │
│ ESP32 DEVKIT              │        Positive (+) ◄─────────────────────────┘             │             │
│                           │        Negative (-) ──┐                                     │             │
│   [VIN / 5V] ◄────────────┘                       │                                     │             │
│   [GND]      ◄────────────────────────────────────┼─────────────────────────────────────┼─────────────┘
│                                                   │                                     │
│   [GPIO 18]  ◄── [ Left Btn (40s) ]       ────┐   │                                     │
│   [GPIO 19]  ◄── [ Mid Btn (Toggle/Stop) ] ───┼───┴─────────► (To Common Ground)        │
│   [GPIO 21]  ◄── [ Right Btn (80s) ]      ────┘                                         │
│                                                                                         │
│   [GPIO 2]   ──► Onboard Blue LED (Status Feedback)                                     │
│                                                                                         │
│   [GPIO 22]  ───[ 220Ω Resistor ]───┐                                                   │
│   (LEDC PWM)                        │                                                   │
└─────────────────────────────────────┼───────────────────────────────────────────────────┼─────────────┐
                                      │                                                   │             │
                                      ▼                                                   │             │
                         ┌─────────────────────────┐                                      │             │
                         │ IRLZ44N MOSFET          │                                      │             │
                         │                         │                                      │             │
                         │ Gate (Pin 1) ◄──────────┘                                      │             │
                         │ Drain (Pin 2 / Tab) ◄──────────────────────────────────────────┘             │
                         │ Source (Pin 3) ──────────────────────────────────────────────────────────────┘
                         │                                                                (To Common Ground)
                         │ * 10kΩ Pull-down Resistor placed
                         │   directly between Gate (Pin 1) and Source (Pin 3)
                         └─────────────────────────┘

* Inductive Spike Protection:
  Solder 1N4007 Diode directly across 12V Pump Motor terminals
  (Cathode stripe ──► Motor Positive / 12V+, Anode ──► Motor Negative / Drain).

* Buffer Capacitor:
  Place a 470µF - 1000µF electrolytic capacitor across 12V Boost VIN+ and VIN- to absorb motor start surge.
```

### Circuit Wiring Walkthrough

1. **Power Path & Battery Protection**:
   * Connect Li-Po battery directly to **`B+`** and **`B-`** on the TP4056 board.
   * **ALL power must exit via `OUT+` and `OUT-`** to maintain DW01A over-discharge, over-current, and short-circuit protection.
   * Connect a miniature 5V step-up booster to `OUT+` / `OUT-`, feeding 5V to the ESP32 **`VIN`** (or `5V`) pin and **`GND`**.
   * Connect the 12V boost converter to `OUT+` / `OUT-`. Place a **470µF–1000µF electrolytic capacitor** directly across `VIN+` and `VIN-`.

2. **Motor & Flyback Diode**:
   * Pump Positive terminal connects directly to **`VOUT+` (12V)** of the 12V Boost Converter.
   * Pump Negative terminal connects to **IRLZ44N Drain (Pin 2 / Center Tab)**.
   * **1N4007 Diode**: Solder directly across motor wires with the white stripe (Cathode) facing **12V+** and the black end (Anode) facing **Drain**.

3. **MOSFET Gate Drive**:
   * Connect **GPIO 22** through a **220Ω** series current-limiting resistor to **Gate (Pin 1)**.
   * Connect a **10kΩ** pull-down resistor directly between **Gate (Pin 1)** and **Source (Pin 3)** to hold the MOSFET fully OFF while the ESP32 is booting.
   * Connect **Source (Pin 3)** to **Common Ground**.

4. **Buttons**:
   * Wire one side of each momentary pushbutton to **GPIO 18** (Left), **GPIO 19** (Middle), and **GPIO 21** (Right).
   * Wire the other side of all three buttons to **ESP32 GND**. (Internal pull-ups + 50ms software noise debounce are active in firmware).

---

## 🧰 Bill of Materials (BOM)

| Component | Specification | Connection / Notes |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32-WROOM-32 / DevKit v1 | 240MHz Dual Core, Wi-Fi OTA, hardware LEDC PWM |
| **MOSFET** | IRLZ44N (Logic-Level N-Channel) | Low-side motor switch (logic-level $V_{GS(th)} < 2.0\text{V}$) |
| **Step-Up Boost (12V)** | Mini DC-DC Step-Up (3.7V $\to$ 12V) | Powers 12V DC pump motor from protected LiPo rail |
| **Step-Up Boost (5V)** | Micro 5V Boost (3.7V $\to$ 5V) | Clean, stable 5.0V supply for ESP32 VIN without thermal throttling |
| **BMS / Charger** | TP4056 + DW01A Module | Safe USB charging and continuous Li-Po battery protection |
| **Flyback Diode** | 1N4007 (or 1N5819 Schottky) | Clamps inductive motor back-EMF spikes |
| **Gate Resistor** | 220 $\Omega$ (Red-Red-Brown, 1/4W) | Limits ESP32 GPIO inrush current to MOSFET gate capacitance |
| **Gate Pull-Down** | 10 k$\Omega$ (Brown-Black-Orange, 1/4W) | Holds Gate LOW during ESP32 power-on / reset |
| **Buffer Capacitor** | 470 $\mu$F – 1000 $\mu$F 16V Electrolytic | Buffers motor startup current spike at boost converter input |
| **Buttons** | 3x Tactile Pushbuttons | Panel-mount buttons for 40s, Toggle/Cancel, and 80s |

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

