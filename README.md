# Smart Watch – Final Version

**Author:** Shirel Orkabi  
**Date:** March 25, 2025

## Overview

Embedded C implementation of a smartwatch running on a dsPIC33 microcontroller with OLED display and ADXL345 accelerometer. The system supports real-time clock display, pedometer tracking, interactive menus, and user input via buttons and tilt.

---

## Key Features

- **Real-Time Clock**
  - Supports both 12-hour (AM/PM) and 24-hour formats
  - Auto-updating date (day/month)

- **Pedometer**
  - Step detection using 3-axis accelerometer
  - Animated pace display with real-time updates
  - Step history visualized as a graph

- **Interactive UI**
  - Menu navigation via physical buttons
  - Time/date settings with tilt-to-save
  - OLED-based graphical feedback

---

## Hardware Requirements

- **Microcontroller:** dsPIC33 or similar
- **Display:** OLED with oledC driver
- **Sensor:** ADXL345 accelerometer via I2C (address `0x3A`)
- **Inputs:** 2 push buttons (RA11, RA12), 2 LEDs
- **Timer:** Timer1 (1Hz) for timekeeping

---

## Build Instructions

1. Open project in **MPLAB X IDE**
2. Ensure `xc.h` and relevant libraries (oledC, delay, I2C) are included
3. Set up oscillator, I2C, GPIO, and Timer1 configuration bits
4. Build and upload firmware to the target board

---

## Usage Instructions

### 📋 Basic Controls

| Action                            | Interaction                      |
|----------------------------------|----------------------------------|
| Enter main menu                  | Long press `BUTTON1`             |
| Scroll up/down in menu           | Press `BUTTON1` / `BUTTON2`      |
| Select menu item                 | Hold `BUTTON1` + `BUTTON2`       |
| Exit menu                        | Select "Exit" or long press `BUTTON1` (graph mode) |
| Save time/date changes           | **Tilt** device downward         |

### 📘 Menu Options

1. `PedometerGraph` – Shows live step graph  
2. `12H/24H` – Toggle time format  
3. `Set Time` – Modify current time  
4. `Set Date` – Modify current date  
5. `Exit` – Return to clock screen