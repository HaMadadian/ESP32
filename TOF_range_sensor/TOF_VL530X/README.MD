# ESP32-C3 Dual VL53L0X ToF Distance Monitor & Logger

IoT prototype for reading two VL53L0X Time-of-Flight distance sensors, displaying live measurements on a phone-accessible web dashboard, and storing timestamped readings persistently on the device with automatic file rotation.

## Features

- **Two VL53L0X sensors** on shared I2C bus with dynamic address assignment via XSHUT pins
- **Continuous ranging** mode at ~4 Hz update rate
- **Live web dashboard** showing real-time distances (accessible from phone on local WiFi)
- **CSV logging** to LittleFS with automatic rotation (new file when size exceeds 512 KB)
- **File manager page** for browsing, downloading, and deleting logs directly from browser
- **JSON API endpoint** (`/data`) for live sensor data
- **WiFi credentials** safely stored in `secrets.h` 
- **Robust error handling** and detailed serial logging with millisecond timestamps

## Hardware Setup

### Required Components

- ESP32-C3 Super Mini development board
- 2 × VL53L0X Time-of-Flight sensor modules (GY-VL53L0XV2 or similar)
- Jumper wires

### Pin Connections

| Signal       | ESP32-C3 Pin | Notes                                      |
|--------------|--------------|--------------------------------------------|
| I2C SDA      | GPIO 8       | Shared data line                           |
| I2C SCL      | GPIO 9       | Shared clock line                          |
| Sensor 1 XSHUT | GPIO 10    | High = enabled (default address 0x29)      |
| Sensor 2 XSHUT | GPIO 2     | High = enabled (changed to 0x2A)           |
| VCC (both)   | 3V3          | 2.8–3.3 V recommended                      |
| GND (both)   | GND          | Common ground                              |



## Screenshots

### 1. Live Dashboard (phone-accessible web interface)

![Dashboard View](screenshots/dashboard.png)
*Real-time distances, status indicators, and auto-refreshing chart*

### 2. File Manager Web Page

![File Manager View](screenshots/file_manager.png)
*Browse, download, and delete stored log files directly from browser*

## Software Architecture

### Embedded Firmware (C)

- **Sensor handling**: Continuous ranging mode, dynamic I2C address assignment
- **Logging**: CSV format with timestamp, distances, and status codes
- **Rotation**: New file created when current file exceeds 512 KB
- **Web server**: HTTP endpoints for dashboard, JSON data, file list, download, delete
- **Credentials**: Stored in `include/secrets.h` 

### Filesystem (LittleFS)

- Persistent storage on internal flash (~1.5 MB usable)
- Survives power cycles
- Rotational CSV files prevent unbounded growth
- Files retrievable via USB (`pio run --target downloadfs`) or web interface

## Data Flow, and Hardware Diagrams
### 1. Hardware Diagram

```mermaid
graph TD
    subgraph ESP32-C3[ESP32-C3 Super Mini]
        A[GPIO 8 - SDA] 
        B[GPIO 9 - SCL]
        C[GPIO 10 - XSHUT1]
        D[GPIO 2 - XSHUT2]
        E[3V3]
        F[GND]
    end

    subgraph Sensor1[VL53L0X Sensor 1]
        G[VCC] 
        H[GND]
        I[SDA]
        J[SCL]
        K[XSHUT]
    end

    subgraph Sensor2[VL53L0X Sensor 2]
        L[VCC]
        M[GND]
        N[SDA]
        O[SCL]
        P[XSHUT]
    end

    A -->|SDA| I
    A -->|SDA| N
    B -->|SCL| J
    B -->|SCL| O
    C -->|HIGH = Enable| K
    D -->|HIGH = Enable| P
    E -->|3.3V| G
    E -->|3.3V| L
    F -->|GND| H
    F -->|GND| M

    classDef esp fill:#2c3e50,stroke:#fff,color:#fff
    classDef sensor fill:#27ae60,stroke:#fff,color:#fff
    class ESP32-C3 esp
    class Sensor1,Sensor2 sensor
```

### 2. High-Level System Architecture

```mermaid
sequenceDiagram
    participant ESP as ESP32-C3
    participant S1 as Sensor 1 (0x29)
    participant S2 as Sensor 2 (0x2A)

    ESP->>S1: XSHUT1 = LOW (reset)
    ESP->>S2: XSHUT2 = LOW (reset)
    Note over ESP,S2: Wait 100 ms for full power-down

    ESP->>S1: XSHUT1 = HIGH
    Note right of S1: Boot + internal init (~500 ms)
    ESP->>S1: begin() → address 0x29
    ESP->>S1: startRangeContinuous()

    ESP->>S1: setAddress(0x30)
    Note right of S1: Now at 0x30

    ESP->>S2: XSHUT2 = HIGH
    Note right of S2: Boot + internal init (~500 ms)
    ESP->>S2: begin() → address 0x29
    ESP->>S2: setAddress(0x2A)
    ESP->>S2: startRangeContinuous()

    Note over ESP,S2: Both sensors now active on unique addresses
```

### 3.  Web Access & Retrieval Flow
```mermaid
flowchart TD
    A[Phone / Laptop] -->|http://192.168.1.xxx| B[WebServer]
    B --> C[Dashboard<br>Live Distances]
    B --> D[JSON API<br>/data]
    B --> E[File Manager<br>/files]
    E --> F[Download Log<br>/log?file=...]
    E --> G[Delete Log<br>/delete?file=...]
    B --> H[LittleFS Storage]
    H --> I[Rotational CSV Files]
```
