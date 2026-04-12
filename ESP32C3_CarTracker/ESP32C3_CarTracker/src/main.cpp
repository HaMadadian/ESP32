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
unsigned long last_log_ms = 0;  // For 1-second logging interval

// ────────────────────────────────────────────────
// Timestamped Logging
// ────────────────────────────────────────────────
// Debug log (serial only, or serial + file if needed)
void log_info(const char* format, ...) {
    char msg[128];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    unsigned long ms = millis();
    Serial.printf("[%07lu.%03lu] INFO: %s\n", ms/1000, ms%1000, msg);

    // Optional: echo debug to current log file (comment out if you don't want this)
    // if (logFile) logFile.printf("[%07lu.%03lu] INFO: %s\n", ms/1000, ms%1000, msg);
}

// Clean CSV-only log (no debug prefix)
void log_csv(unsigned long ms, double lat, double lon, uint32_t sats,
             float ax, float ay, float az, float gx, float gy, float gz,
             float temp, float vel) {
    if (!logFile) return;

    char line[256];
    snprintf(line, sizeof(line),
             "%lu,%.8f,%.8f,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.3f\n",
             ms, lat, lon, sats, ax, ay, az, gx, gy, gz, temp, vel);

    logFile.print(line);
    logFile.flush();
}

// ────────────────────────────────────────────────
// Rotate log file (size-based rotation)
void rotate_log_file() {
    if (!logFile) {
        current_log_file = "/log_0.csv";
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
// Log data every 1 second
void log_data() {
    if (millis() - last_log_ms < 1000) return;
    last_log_ms = millis();

    rotate_log_file();
    if (!logFile) return;

    log_csv(millis(), lat, lon, satellites,
            acc.x, acc.y, acc.z,
            gyr.x, gyr.y, gyr.z,
            temperature, velocity);

    log_info("Logged (1s): pos=%.6f,%.6f | v=%.3f m/s", lat, lon, velocity);
}

// ────────────────────────────────────────────────
// Read MPU6500
void read_mpu() {
    acc = myIMU.getGValues();
    gyr = myIMU.getGyrValues();
    temperature = myIMU.getTemperature();

    log_info("MPU: ax=%.3f g, ay=%.3f g, az=%.3f g | gx=%.1f °/s, gy=%.1f °/s, gz=%.1f °/s | temp=%.1f °C",
             acc.x, acc.y, acc.z, gyr.x, gyr.y, gyr.z, temperature);
}

// ────────────────────────────────────────────────
// JSON API endpoint
void handle_data() {
    char json[512];
    snprintf(json, sizeof(json),
             "{\"timestamp\":%lu,"
             "\"lat\":%.8f,\"lon\":%.8f,\"sats\":%u,"
             "\"accelX\":%.3f,\"accelY\":%.3f,\"accelZ\":%.3f,"
             "\"gyroX\":%.3f,\"gyroY\":%.3f,\"gyroZ\":%.3f,"
             "\"temperature\":%.2f,\"velocity\":%.3f}",
             millis(), lat, lon, satellites,
             acc.x, acc.y, acc.z,
             gyr.x, gyr.y, gyr.z,
             temperature, velocity);

    server.send(200, "application/json", json);
}

// ────────────────────────────────────────────────
// Professional Dashboard
void handle_root() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>ESP32 Car Tracker Dashboard</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
    <style>
        body {font-family:'Segoe UI',Arial,sans-serif; background:#0f172a; color:#e2e8f0; margin:0; padding:20px;}
        h1 {color:#60a5fa; text-align:center; margin-bottom:30px;}
        .grid {display:grid; grid-template-columns:repeat(auto-fit,minmax(400px,1fr)); gap:20px; max-width:1400px; margin:auto;}
        .card {background:#1e2937; border-radius:12px; padding:20px; box-shadow:0 4px 15px rgba(0,0,0,0.3);}
        .value {font-size:2.8em; font-weight:bold; margin:10px 0; color:#60a5fa;}
        .label {font-size:1.1em; color:#94a3b8;}
        canvas {max-height:280px;}
        .gps {background:#1e2937; padding:20px; border-radius:12px; text-align:center;}
        .gps-value {font-size:1.6em; color:#34d399;}
        .temp-card {text-align:center;}
        .temp-value {font-size:3.5em; font-weight:bold;}
        .temp-hot {color:#ef4444;}
        .temp-normal {color:#34d399;}
        .temp-cold {color:#60a5fa;}
        @media (max-width:900px) {.grid {grid-template-columns:1fr;}}
    </style>
</head>
<body>
    <h1>ESP32 Car Tracker – Live Dashboard</h1>
    <div class="grid">
        <div class="card">
            <h2>Acceleration (g)</h2>
            <canvas id="accelChart"></canvas>
        </div>
        <div class="card">
            <h2>Gyroscope (°/s)</h2>
            <canvas id="gyroChart"></canvas>
        </div>
        <div class="card">
            <h2>Velocity (m/s)</h2>
            <canvas id="velChart"></canvas>
        </div>
        <div class="card temp-card">
            <h2>Temperature (°C)</h2>
            <div id="tempValue" class="temp-value">--</div>
            <div class="label">Board Temp</div>
        </div>
        <div class="card gps">
            <h2>GPS Status</h2>
            <div class="value" id="lat">0.000000</div>
            <div class="label">Latitude</div>
            <div class="value" id="lon">0.000000</div>
            <div class="label">Longitude</div>
            <div class="value" id="sats">0</div>
            <div class="label">Satellites</div>
            <div id="fix" style="margin-top:15px; font-size:1.4em;">NO FIX</div>
        </div>
    </div>

    <script>
        let accelChart, gyroChart, velChart;
        let history = {ax:[],ay:[],az:[],gx:[],gy:[],gz:[],vel:[]};

        function initCharts() {
            const cfg = {type:'line', options:{animation:false, responsive:true, scales:{y:{beginAtZero:false}}}};
            accelChart = new Chart(document.getElementById('accelChart'), {...cfg, data:{labels:[], datasets:[
                {label:'X', borderColor:'#ef4444', data:[]},
                {label:'Y', borderColor:'#22c55e', data:[]},
                {label:'Z', borderColor:'#3b82f6', data:[]}
            ]}});
            gyroChart = new Chart(document.getElementById('gyroChart'), {...cfg, data:{labels:[], datasets:[
                {label:'X', borderColor:'#ef4444', data:[]},
                {label:'Y', borderColor:'#22c55e', data:[]},
                {label:'Z', borderColor:'#3b82f6', data:[]}
            ]}});
            velChart = new Chart(document.getElementById('velChart'), {...cfg, data:{labels:[], datasets:[
                {label:'Velocity', borderColor:'#eab308', data:[]}
            ]}});
        }

        function updateDashboard() {
            fetch('/data')
                .then(r => r.json())
                .then(d => {
                    // Debug: log received data to browser console
                    console.log("Received data:", d);

                    // GPS
                    document.getElementById('lat').textContent = d.lat.toFixed(6);
                    document.getElementById('lon').textContent = d.lon.toFixed(6);
                    document.getElementById('sats').textContent = d.sats;
                    document.getElementById('fix').textContent = d.sats > 3 ? 'FIX ACQUIRED' : 'NO FIX';
                    document.getElementById('fix').style.color = d.sats > 3 ? '#34d399' : '#f59e0b';

                    // Temperature
                    let tempEl = document.getElementById('tempValue');
                    tempEl.textContent = d.temperature.toFixed(1);
                    if (d.temperature > 45) tempEl.className = 'temp-value temp-hot';
                    else if (d.temperature < 10) tempEl.className = 'temp-value temp-cold';
                    else tempEl.className = 'temp-value temp-normal';

                    // Charts
                    let t = (d.timestamp/1000).toFixed(1);
                    history.ax.push(d.accelX); history.ay.push(d.accelY); history.az.push(d.accelZ);
                    history.gx.push(d.gyroX); history.gy.push(d.gyroY); history.gz.push(d.gyroZ);
                    history.vel.push(d.velocity);

                    if (history.ax.length > 60) {
                        Object.keys(history).forEach(k => history[k].shift());
                    }

                    accelChart.data.labels = Array.from({length:history.ax.length}, (_,i)=> t - (history.ax.length-i)*1);
                    accelChart.data.datasets[0].data = history.ax;
                    accelChart.data.datasets[1].data = history.ay;
                    accelChart.data.datasets[2].data = history.az;
                    accelChart.update();

                    gyroChart.data.labels = Array.from({length:history.gx.length}, (_,i)=> t - (history.gx.length-i)*1);
                    gyroChart.data.datasets[0].data = history.gx;
                    gyroChart.data.datasets[1].data = history.gy;
                    gyroChart.data.datasets[2].data = history.gz;
                    gyroChart.update();

                    velChart.data.labels = Array.from({length:history.vel.length}, (_,i)=> t - (history.vel.length-i)*1);
                    velChart.data.datasets[0].data = history.vel;
                    velChart.update();
                })
                .catch(err => console.error("Dashboard fetch error:", err));
        }

        initCharts();
        setInterval(updateDashboard, 1000);
        updateDashboard();
    </script>
</body>
</html>
)rawliteral";

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
// Download log file
void handle_download() {
    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "Missing file parameter");
        return;
    }

    String filename = server.arg("file");
    if (!filename.startsWith("/")) filename = "/" + filename;

    if (!LittleFS.exists(filename)) {
        server.send(404, "text/plain", "File not found");
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
    if (!filename.startsWith("/")) filename = "/" + filename;

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
    last_log_ms = millis();
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

    // Log every 1 second
    log_data();

    delay(250);  // keep loop responsive
}