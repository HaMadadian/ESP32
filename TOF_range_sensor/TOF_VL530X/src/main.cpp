#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include "secrets.h"  // Your WiFi credentials

// ────────────────────────────────────────────────
// Pin Configuration
// ────────────────────────────────────────────────
#define I2C_SDA   8
#define I2C_SCL   9
#define XSHUT1   10   // Sensor 1 enable
#define XSHUT2    2   // Sensor 2 enable


// ────────────────────────────────────────────────
// Global Objects
// ────────────────────────────────────────────────
Adafruit_VL53L0X sensor1;
Adafruit_VL53L0X sensor2;
WebServer server(80);

// Log rotation
File logFile;
String current_log_file = "";

// ────────────────────────────────────────────────
// Timestamped Logging
// ────────────────────────────────────────────────
void log_info(const char* format, ...) {
    char msg[128];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    unsigned long ms = millis();
    Serial.printf("[%07lu.%03lu] INFO: %s\n", ms/1000, ms%1000, msg);

    if (logFile) {
        logFile.printf("[%07lu.%03lu] INFO: %s\n", ms/1000, ms%1000, msg);
    }
}

// ────────────────────────────────────────────────
// Rotate log file (daily approximation via millis)
void rotate_log_file() {
    String new_file = "/log_" + String(millis() / 86400000) + ".txt";
    if (new_file != current_log_file) {
        if (logFile) logFile.close();
        logFile = LittleFS.open(new_file, FILE_APPEND);
        if (logFile) {
            current_log_file = new_file;
            log_info("Log rotated → %s", new_file.c_str());
        } else {
            log_info("Failed to create new log file");
        }
    }
}

// ────────────────────────────────────────────────
// Simple I2C bus scanner (your proven function)
void scan_i2c() {
    Serial.println("\n[I2C] Scanning bus...");
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[I2C] Device found at 0x%02X\n", addr);
            found++;
        }
    }
    if (found == 0) Serial.println("[I2C] No devices detected!");
    else Serial.printf("[I2C] Scan complete – %d device(s) found\n", found);
}

// ────────────────────────────────────────────────
// JSON data handler (polls every 1 second from client)
void handle_data() {
    VL53L0X_RangingMeasurementData_t m1;
    sensor1.getRangingMeasurement(&m1, false);
    VL53L0X_RangingMeasurementData_t m2;
    sensor2.getRangingMeasurement(&m2, false);

    int dist1 = (m1.RangeStatus == 4) ? -1 : m1.RangeMilliMeter;
    int dist2 = (m2.RangeStatus == 4) ? -1 : m2.RangeMilliMeter;

    char json[256];
    snprintf(json, sizeof(json), 
             "{\"sensor1\":%d,\"sensor2\":%d,\"status1\":%d,\"status2\":%d,\"time\":%lu}",
             dist1, dist2, m1.RangeStatus, m2.RangeStatus, millis());
    
    server.send(200, "application/json", json);
}

// ────────────────────────────────────────────────
// Web page handler
void handle_root() {
    rotate_log_file();

    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Dual VL53L0X Monitor</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {font-family:Arial; text-align:center; background:#f8f9fa; color:#333;}
        h1 {color:#2c3e50;}
        .dist {font-size:4em; font-weight:bold; margin:40px 0;}
        .s1 {color:#e74c3c;}
        .s2 {color:#27ae60;}
        .time {font-size:1.2em; color:#7f8c8d;}
        .error {color:#e74c3c; font-weight:bold;}
    </style>
</head>
<body>
    <h1>Dual VL53L0X Distance Monitor</h1>
    <div class="dist s1">Sensor 1: <span id="dist1">--</span> mm</div>
    <div class="dist s2">Sensor 2: <span id="dist2">--</span> mm</div>
    <p class="time">Last update: <span id="time">--</span> ms</p>
    <script>
        function updateSensors() {
            fetch('/data')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('dist1').textContent = data.sensor1 < 0 ? 'Error' : data.sensor1;
                    document.getElementById('dist2').textContent = data.sensor2 < 0 ? 'Error' : data.sensor2;
                    document.getElementById('time').textContent = data.time;
                })
                .catch(error => console.error('Error fetching data:', error));
        }
        // Update every 1 second
        updateSensors();
        setInterval(updateSensors, 1000);
    </script>
</body>
</html>
)rawliteral";

    server.send(200, "text/html", html);
}

// ────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1500);  // USB CDC + I2C stability
    log_info("ESP32-C3 Dual VL53L0X System starting");

    // LittleFS
    if (!LittleFS.begin(true)) {  // true = format on failure
        log_info("LittleFS mount/format failed – logs serial only");
    } else {
        log_info("LittleFS mounted");
    }
    rotate_log_file();

    // I2C
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);

    // XSHUT pins
    pinMode(XSHUT1, OUTPUT);
    pinMode(XSHUT2, OUTPUT);
    digitalWrite(XSHUT1, LOW);
    digitalWrite(XSHUT2, LOW);
    delay(100);  // full power-down

    log_info("Starting sensor initialization sequence...");

    // Sensor 1
    log_info("Enabling Sensor 1");
    digitalWrite(XSHUT1, HIGH);
    delay(500);  // long stabilization

    scan_i2c();  // should see 0x29

    if (!sensor1.begin()) {
        log_info("Sensor1 begin() FAILED");
        while (1) delay(1000);
    }

    sensor1.startRangeContinuous();
    log_info("Sensor1 ready (continuous mode)");

    // Change sensor1 address to avoid conflict
    sensor1.setAddress(0x30);
    log_info("Sensor1 address changed to 0x30");
    delay(50);

    // Sensor 2
    log_info("Enabling Sensor 2");
    digitalWrite(XSHUT2, HIGH);
    delay(500);

    scan_i2c();  // should see 0x29 (default for sensor2) + 0x30 (sensor1)

    if (!sensor2.begin()) {
        log_info("Sensor2 begin() FAILED");
        while (1) delay(1000);
    }

    sensor2.setAddress(0x2A);
    sensor2.startRangeContinuous();
    log_info("Sensor2 ready at 0x2A (continuous mode)");

    scan_i2c();  // final check: should see 0x30 and 0x2A

    log_info("Both sensors initialized successfully");

    // WiFi
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);  // Clear previous connections
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    log_info("Connecting to WiFi: %s", WIFI_SSID);


    uint32_t timeout = millis() + 15000;
    while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
        delay(500);
        log_info("WiFi status: %d", WiFi.status());
    }

    if (WiFi.status() == WL_CONNECTED) {
        log_info("WiFi connected – IP: %s", WiFi.localIP().toString().c_str());
        server.on("/", handle_root);
        server.on("/data", handle_data);
        server.begin();
        log_info("Web server started – access http://%s", WiFi.localIP().toString().c_str());
    } else {
        log_info("WiFi failed – no remote webpage");
    }
}

// ────────────────────────────────────────────────
void loop() {
    server.handleClient();
    rotate_log_file();

    VL53L0X_RangingMeasurementData_t m1;
    sensor1.getRangingMeasurement(&m1, false);
    VL53L0X_RangingMeasurementData_t m2;
    sensor2.getRangingMeasurement(&m2, false);

    int dist1 = (m1.RangeStatus == 4) ? -1 : m1.RangeMilliMeter;
    int dist2 = (m2.RangeStatus == 4) ? -1 : m2.RangeMilliMeter;

    log_info("S1: %4d mm (status %d) | S2: %4d mm (status %d)", dist1, m1.RangeStatus, dist2, m2.RangeStatus);

    delay(250);  // 4 Hz – comfortable for phone refresh
}