
# ESP32-C3 Car Tracker – Phase 1

Multi-sensor data logger and basic dead-reckoning prototype running on **ESP32-C3 Super Mini**.

## Project Overview

This is the **first production-oriented phase** of a car tracking/logging system.  
The goal is to reliably read and log data from:

- **GY-GPS6MV2** (NEO-6M) GPS module → position, speed, satellite info
- **MPU-9250** (9-axis IMU) → acceleration and angular velocity
- Simple **dead reckoning** (velocity integration from accelerometer with bias correction)

All logs are timestamped (millis-based with ms precision) and written to **SD card** (persistent across reboots).

### Current Features

- Hardware UART1 for GPS (reliable, no SoftwareSerial overhead)
- I2C for MPU-9250 with robust initialization and debug logging
- Structured logging (INFO / WARN / ERROR levels) with millisecond timestamps
- Dead reckoning demo: velocity integration + GPS reset when fix available
- Persistent logging to SD card (`/tracker.log`)
- SD card initialized with explicit SPI pins 
- Retry logic for SD init (handles power-up transients)

### Hardware Requirements

- **ESP32-C3 Super Mini** development board
- **GY-GPS6MV2** (or NEO-6M) GPS module
- **MPU-9250** 9-axis IMU module (AD0 tied to GND → address 0x68)
- **MicroSD card module** (with level shifters / regulator)
- **32 GB microSD card** (FAT32 formatted, full-size or with good adapter)

### Pinout (ESP32-C3 Super Mini)

| Component        | ESP32-C3 Pin | GPIO  | Direction / Note                     |
|------------------|--------------|-------|--------------------------------------|
| GPS TX           | GPIO4        | GPIO4 | UART1 RX (GPS → ESP)                 |
| GPS RX           | GPIO5        | GPIO5 | UART1 TX (ESP → GPS)                 |
| MPU-9250 SDA     | GPIO8        | GPIO8 | I2C SDA                              |
| MPU-9250 SCL     | GPIO9        | GPIO9 | I2C SCL                              |
| MPU-9250 AD0     | GND          | —     | Forces address 0x68                  |
| SD CS            | GPIO7        | GPIO7 | Chip Select (moved from GPIO1)       |
| SD MOSI          | GPIO2        | GPIO2 | SPI MOSI                             |
| SD MISO          | GPIO3        | GPIO3 | SPI MISO                             |
| SD SCK           | GPIO10       | GPIO10| SPI Clock                            |
| SD VCC           | 3V3 or 5V    | —     | 3.3 V native or 5 V if module has LDO|
| SD GND           | GND          | —     | Common ground                        |

**Important**: If your SD module has **no onboard 3.3 V regulator**, connect VCC to **5V** pin (safe – logic is still shifted to 3.3 V).

### Software Requirements

- PlatformIO (VS Code extension)
- ESP32 Arduino core (managed by PlatformIO)

**platformio.ini** (final working version):

```ini
[env:esp32c3]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
monitor_speed = 115200
monitor_filters = esp32_exception_decoder

lib_deps = 
    mikalhart/TinyGPSPlus@^1.0.2
    adafruit/Adafruit MPU6050@^2.2.4
    adafruit/Adafruit BusIO@^1.16.1
    adafruit/Adafruit Unified Sensor@^1.1.14

build_flags = 
    -D ARDUINO_USB_CDC_ON_BOOT=1
    -D ARDUINO_USB_MODE=1
    -DCORE_DEBUG_LEVEL=ARDUHAL_LOG_LEVEL_INFO

board_build.filesystem = littlefs
```

### Current Limitations & Known Issues

- **GPS** needs clear sky view for fix (cold start can take 30–180 s)
- **Dead reckoning** is naive (velocity drifts due to uncorrected bias/noise)
- **SD card** init can be flaky on first try → retry logic included
- No daily log rotation yet (file grows indefinitely)
- No real Unix timestamp yet (uses millis() – GPS time integration next)

### How to Run & Test

1. Connect board via USB
2. Build & upload: `pio run --target upload`
3. Open serial monitor: `pio device monitor`
4. Keep board still for ~2 s after power-on (bias calibration)
5. Take outdoors → wait for GPS fix (watch "Sats in view" increase)
6. Remove SD card → check `/tracker.log` on PC (all logs should be there)

### Next Planned Phases

- Phase 2: Daily rolling logs (`tracker_2026-02-26.log`, auto-create new file at midnight)
- Phase 3: Real Unix timestamp from GPS (`gps.date` + `gps.time`)
- Phase 4: Proper IMU fusion (Madgwick filter) + zero-velocity updates
- Phase 5: Web server + OTA + map visualization of logged track
- Phase 6: Deep sleep + power management for battery operation

### License

MIT – feel free to use, modify, and share.

### Author

Hamed Madadian / https://github.com/HaMadadian / eng.madadian@gmail.com
