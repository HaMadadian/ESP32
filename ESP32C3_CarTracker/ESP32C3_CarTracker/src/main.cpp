#include <Arduino.h>
#include <Wire.h>
#include <MPU6500_WE.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <TinyGPSPlus.h>
#include "secrets.h"

// ────────────────────────────────────────────────
// Pin Configuration
// ────────────────────────────────────────────────
#define I2C_SDA     8
#define I2C_SCL     9
#define GPS_RX      4
#define GPS_TX      5

// ────────────────────────────────────────────────
// Global Objects
// ────────────────────────────────────────────────
MPU6500_WE myIMU;
HardwareSerial gpsSerial(1);
TinyGPSPlus gps;
WebServer server(80);

// Log management
File logFile;
String current_log_file = "";

// Sensor & navigation data
xyzFloat acc, gyr;
float temperature = 0.0f;
float velocity = 0.0f;
unsigned long last_update = 0;
double lat = 0.0, lon = 0.0;
uint32_t satellites = 0;

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
// Check free space before writing (prevent flash full errors)
bool check_littlefs_space() {
    uint64_t used = LittleFS.usedBytes();
    uint64_t total = LittleFS.totalBytes();
    float percent = (float)used / total * 100.0f;

    if (percent > 90.0f) {
        log_info("WARNING: LittleFS almost full (%.1f%% used) – consider deleting old logs", percent);
        return false;
    }
    return true;
}

// ────────────────────────────────────────────────
// Rotate log file (size-based + free space check)
void rotate_log_file() {
    if (!logFile) {
        current_log_file = "/log_0.csv";
        if (!check_littlefs_space()) {
            log_info("Skipping log creation – flash almost full");
            return;
        }
        logFile = LittleFS.open(current_log_file, FILE_APPEND);
        if (logFile) {
            log_info("Created initial log: %s", current_log_file.c_str());
            logFile.println("timestamp_ms,lat,lon,sats,ax,ay,az,gx,gy,gz,temp,velocity");
        } else {
            log_info("Failed to create initial log file");
        }
        return;
    }

    if (logFile.size() < 512 * 1024) return;

    logFile.close();

    if (!check_littlefs_space()) {
        log_info("Skipping rotation – flash almost full");
        return;
    }

    int index = 1;
    String new_file;
    do {
        new_file = "/log_" + String(index) + ".csv";
        index++;
    } while (LittleFS.exists(new_file));

    logFile = LittleFS.open(new_file, FILE_APPEND);
    if (logFile) {
        current_log_file = new_file;
        log_info("Rotated log file → %s", new_file.c_str());
        logFile.println("timestamp_ms,lat,lon,sats,ax,ay,az,gx,gy,gz,temp,velocity");
    } else {
        log_info("Failed to create new log file: %s", new_file.c_str());
    }
}

// ────────────────────────────────────────────────
// Log sensor & GPS data to CSV
void log_data() {
    rotate_log_file();

    if (!logFile) return;

    unsigned long ms = millis();

    char line[256];
    snprintf(line, sizeof(line),
             "%lu,%.8f,%.8f,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.3f\n",
             ms, lat, lon, satellites,
             acc.x, acc.y, acc.z,
             gyr.x, gyr.y, gyr.z,
             temperature, velocity);

    logFile.print(line);
    logFile.flush();

    log_info("Logged: t=%lu | pos=%.6f,%.6f | v=%.3f m/s", ms, lat, lon, velocity);
}

// ────────────────────────────────────────────────
// JSON API endpoint
void handle_data() {
    char json[512];
    snprintf(json, sizeof(json),
             "{\"timestamp\":%lu,"
             "\"lat\":%.8f,\"lon\":%.8f,\"sats\":%u,"
             "\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
             "\"gx\":%.3f,\"gy\":%.3f,\"gz\":%.3f,"
             "\"temp\":%.2f,\"velocity\":%.3f}",
             millis(), lat, lon, satellites,
             acc.x, acc.y, acc.z,
             gyr.x, gyr.y, gyr.z,
             temperature, velocity);

    server.send(200, "application/json", json);
}

// ────────────────────────────────────────────────
// Root page
void handle_root() {
    String html = "<!DOCTYPE html><html><head><title>ESP32 Tracker</title>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<style>body{font-family:Arial;text-align:center;background:#f0f0f0;}"
                  "h1{color:#333;}.data{font-size:1.4em;margin:20px;}</style></head>"
                  "<body><h1>ESP32 Car Tracker</h1>"
                  "<div class='data'>IP: " + WiFi.localIP().toString() + "</div>"
                  "<div class='data'><a href='/data'>JSON API</a> | <a href='/files'>File Manager</a></div>"
                  "<p>Check serial for logs</p></body></html>";

    server.send(200, "text/html", html);
}

// ────────────────────────────────────────────────
// File list JSON
void handle_list() {
    String json = "[";
    bool first = true;

    File root = LittleFS.open("/");
    File file = root.openNextFile();

    while (file) {
        if (!first) json += ",";
        first = false;

        json += "{\"name\":\"" + String(file.name()) + "\",\"size\":" + String(file.size()) + "}";
        file = root.openNextFile();
    }

    json += "]";

    server.send(200, "application/json", json);
}

// ────────────────────────────────────────────────
// Download log file (fix: accept with/without leading slash)
void handle_download() {
    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "Missing file parameter");
        return;
    }

    String filename = server.arg("file");
    // Normalize path (add leading / if missing)
    if (!filename.startsWith("/")) {
        filename = "/" + filename;
    }

    if (!LittleFS.exists(filename)) {
        server.send(404, "text/plain", "File not found: " + filename);
        return;
    }

    File f = LittleFS.open(filename, "r");
    if (!f) {
        server.send(500, "text/plain", "Cannot open file");
        return;
    }

    server.streamFile(f, "text/csv");
    f.close();
}

// ────────────────────────────────────────────────
// Delete log file
void handle_delete() {
    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "Missing file parameter");
        return;
    }

    String filename = server.arg("file");
    if (!filename.startsWith("/")) {
        filename = "/" + filename;
    }

    if (logFile && filename == current_log_file) {
        logFile.close();
        current_log_file = "";
    }

    if (LittleFS.remove(filename)) {
        log_info("Deleted file: %s", filename.c_str());
        server.send(200, "text/plain", "Deleted");
    } else {
        server.send(500, "text/plain", "Delete failed");
    }
}

// ────────────────────────────────────────────────
// File manager page
void handle_files() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Log Files</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {font-family:Arial; text-align:center; background:#f8f9fa;}
        h1 {color:#2c3e50;}
        table {margin:auto; border-collapse:collapse; width:90%;}
        th,td {border:1px solid #ccc; padding:10px;}
        th {background:#2c3e50; color:white;}
        button {padding:6px 12px; margin:2px; cursor:pointer;}
    </style>
</head>
<body>
    <h1>Log Files on ESP32</h1>
    <table>
        <thead><tr><th>File</th><th>Size (bytes)</th><th>Download</th><th>Delete</th></tr></thead>
        <tbody id="files"></tbody>
    </table>

    <script>
        fetch('/list')
            .then(r => r.json())
            .then(files => {
                let tbody = document.getElementById('files');
                files.forEach(f => {
                    let tr = document.createElement('tr');
                    tr.innerHTML = `
                        <td>${f.name}</td>
                        <td>${f.size}</td>
                        <td><a href="/log?file=${encodeURIComponent(f.name)}" target="_blank">Download</a></td>
                        <td><button onclick="deleteFile('${encodeURIComponent(f.name)}')">Delete</button></td>
                    `;
                    tbody.appendChild(tr);
                });
            });

        function deleteFile(name) {
            if (!confirm("Delete " + name + "?")) return;
            fetch('/delete?file=' + name)
                .then(r => r.text())
                .then(msg => {
                    alert(msg);
                    location.reload();
                });
        }
    </script>
</body>
</html>
)rawliteral";

    server.send(200, "text/html", html);
}

// ────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1500);
    log_info("ESP32-C3 Car Tracker starting");

    // LittleFS
    if (!LittleFS.begin(true)) {
        log_info("LittleFS mount/format failed – no persistent logging");
    } else {
        log_info("LittleFS mounted successfully");
    }

    // I2C
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);

    // MPU6500
    if (!myIMU.init()) {
        log_info("MPU6500 init FAILED – check wiring or I2C pull-ups");
        while (1) delay(1000);
    }

    log_info("MPU6500 initialized – WHO_AM_I = 0x%02X", myIMU.whoAmI());

    myIMU.setAccRange(MPU9250_ACC_RANGE_8G);
    myIMU.setGyrRange(MPU9250_GYRO_RANGE_500);
    myIMU.setAccDLPF(MPU9250_DLPF_6);
    myIMU.setGyrDLPF(MPU9250_DLPF_6);

    myIMU.autoOffsets();
    log_info("MPU6500 auto-offsets applied");

    // GPS
    gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
    log_info("GPS UART started on pins RX=%d, TX=%d", GPS_RX, GPS_TX);

    // WiFi
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    log_info("Connecting to WiFi: %s", WIFI_SSID);

    uint32_t timeout = millis() + 20000;
    while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
        delay(500);
        log_info("WiFi status: %d", WiFi.status());
    }

    if (WiFi.status() == WL_CONNECTED) {
        log_info("WiFi connected – IP: %s", WiFi.localIP().toString().c_str());
        server.on("/", handle_root);
        server.on("/data", handle_data);
        server.on("/files", handle_files);
        server.on("/list", handle_list);
        server.on("/log", handle_download);
        server.on("/delete", handle_delete);
        server.begin();
        log_info("Web server started – access http://%s", WiFi.localIP().toString().c_str());
    } else {
        log_info("WiFi connection failed – continuing without web interface");
    }

    last_update = millis();
}

// ────────────────────────────────────────────────
void loop() {
    server.handleClient();

    // Read GPS
    while (gpsSerial.available() > 0) {
        gps.encode(gpsSerial.read());
    }

    if (gps.location.isValid()) {
        lat = gps.location.lat();
        lon = gps.location.lng();
        satellites = gps.satellites.value();
        log_info("GPS Fix: lat=%.6f, lon=%.6f, sats=%d", lat, lon, satellites);
    } else {
        log_info("GPS no valid fix yet (age=%lu ms)", gps.location.age());
    }

    // Read MPU6500
    acc = myIMU.getGValues();
    gyr = myIMU.getGyrValues();
    temperature = myIMU.getTemperature();

    // Log MPU values every cycle
    log_info("MPU: ax=%.3f g, ay=%.3f g, az=%.3f g | gx=%.1f °/s, gy=%.1f °/s, gz=%.1f °/s | temp=%.1f °C",
             acc.x, acc.y, acc.z, gyr.x, gyr.y, gyr.z, temperature);

    // Dead reckoning
    unsigned long now = millis();
    float dt = (now - last_update) / 1000.0f;
    last_update = now;

    velocity += acc.x * dt;
    if (fabs(acc.x) < 0.03f) {
        velocity *= 0.98f;
    }

    // Log everything
    log_data();

    delay(1000);
}