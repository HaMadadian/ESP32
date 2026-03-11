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

// Log management
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

    if (!logFile) {
        current_log_file = "/log_0.csv";
        logFile = LittleFS.open(current_log_file, FILE_APPEND);

        if (logFile) {
            log_info("New CSV log: %s", current_log_file.c_str());
            logFile.println("timestamp_ms,s1_mm,s1_status,s2_mm,s2_status");
        }
        return;
    }

    if (logFile.size() < 512 * 1024)
        return;

    logFile.close();

    int index = 0;
    String new_file;

    do {
        index++;
        new_file = "/log_" + String(index) + ".csv";
    } while (LittleFS.exists(new_file));

    logFile = LittleFS.open(new_file, FILE_APPEND);

    if (logFile) {
        current_log_file = new_file;
        log_info("Rotated log file -> %s", new_file.c_str());
        logFile.println("timestamp_ms,s1_mm,s1_status,s2_mm,s2_status");
    }
}

// ────────────────────────────────────────────────
// I2C bus scanner (used during setup to verify sensor presence and addresses)
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
//----------------------------------------------------------
// endpoint to download the log
void handle_download() {

    if (!server.hasArg("file")) {
        server.send(400,"text/plain","missing file");
        return;
    }

    String filename = server.arg("file");

    File f = LittleFS.open(filename,"r");

    if(!f){
        server.send(404,"text/plain","file not found");
        return;
    }

    server.streamFile(f,"text/csv");
    f.close();
}


// ────────────────────────────────────────────────
// Web page handler
void handle_root() {
    rotate_log_file(); // ensure current log is active

    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>ESP32 Dual VL53L0X Dashboard</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body { font-family: Arial, sans-serif; background:#f4f6f8; color:#333; margin:0; padding:0;}
        header { background:#2c3e50; color:white; padding:15px; text-align:center; }
        h1 { margin:0; font-size:1.8em; }
        main { padding:20px; max-width:900px; margin:auto; }
        .sensors { display:flex; justify-content:space-around; margin-bottom:30px; }
        .sensor { text-align:center; background:white; border-radius:10px; padding:20px; flex:1; margin:5px; box-shadow:0 2px 5px rgba(0,0,0,0.15);}
        .sensor h2 { margin:10px 0; font-size:1.2em; }
        .sensor .value { font-size:2.5em; font-weight:bold; margin:10px 0; }
        .status-ok { color: #27ae60; }
        .status-error { color: #e74c3c; }
        .chart-container { background:white; padding:20px; border-radius:10px; box-shadow:0 2px 5px rgba(0,0,0,0.15);}
        .info { margin-top:20px; text-align:center; }
        a.button { display:inline-block; padding:10px 15px; background:#2c3e50; color:white; border-radius:5px; text-decoration:none; margin-top:10px; }
    </style>
</head>
<body>
<header>
    <h1>ESP32 Dual VL53L0X Dashboard</h1>
</header>
<main>
    <div class="sensors">
        <div class="sensor">
            <h2>Sensor 1</h2>
            <div class="value" id="s1Value">--</div>
            <div id="s1Status" class="status-ok">OK</div>
        </div>
        <div class="sensor">
            <h2>Sensor 2</h2>
            <div class="value" id="s2Value">--</div>
            <div id="s2Status" class="status-ok">OK</div>
        </div>
    </div>

    <div class="chart-container">
        <canvas id="sensorChart" height="150"></canvas>
    </div>

    <div class="info">
        <p>ESP32 IP: <span id="espIP">--</span></p>
        <p>System uptime: <span id="uptime">--</span> s</p>
        <a href="/log?file=/log_0.csv" class="button" target="_blank">Download Current Log</a>
    </div>
</main>

<script>
let chartData = {
    labels: [],
    datasets: [
        { label: 'Sensor 1 (mm)', data: [], borderColor:'#e74c3c', fill:false },
        { label: 'Sensor 2 (mm)', data: [], borderColor:'#27ae60', fill:false }
    ]
};

let ctx = document.getElementById('sensorChart').getContext('2d');
let sensorChart = new Chart(ctx, { type:'line', data: chartData, options:{
    responsive:true,
    animation:false,
    scales:{ x:{ title:{ display:true, text:'Time (s)' }}, y:{ beginAtZero:true, title:{ display:true, text:'Distance (mm)' } } }
}});

function updateSensors() {
    fetch('/data')
        .then(r=>r.json())
        .then(data=>{
            document.getElementById('s1Value').textContent = data.sensor1 < 0 ? 'Error' : data.sensor1;
            document.getElementById('s2Value').textContent = data.sensor2 < 0 ? 'Error' : data.sensor2;

            document.getElementById('s1Status').textContent = data.status1==4 ? 'Error' : 'OK';
            document.getElementById('s1Status').className = data.status1==4 ? 'status-error':'status-ok';

            document.getElementById('s2Status').textContent = data.status2==4 ? 'Error' : 'OK';
            document.getElementById('s2Status').className = data.status2==4 ? 'status-error':'status-ok';

            // Update chart
            let t = (data.time/1000).toFixed(1);
            chartData.labels.push(t);
            chartData.datasets[0].data.push(data.sensor1);
            chartData.datasets[1].data.push(data.sensor2);

            if(chartData.labels.length>50){ // keep last 50 points
                chartData.labels.shift();
                chartData.datasets[0].data.shift();
                chartData.datasets[1].data.shift();
            }

            sensorChart.update();

            document.getElementById('espIP').textContent = window.location.hostname;
            document.getElementById('uptime').textContent = (data.time/1000).toFixed(1);
        })
        .catch(console.error);
}

updateSensors();
setInterval(updateSensors,1000);
</script>
</body>
</html>
)rawliteral";

    server.send(200,"text/html",html);
}

// ────────────────────────────────────────────────

// CSV log format: timestamp_ms,sensor1_mm,sensor1_status,sensor2_mm,sensor2_status
void log_sensor_data(int dist1, int status1, int dist2, int status2) {
    rotate_log_file();  // ensure current file is open

    if (!logFile) return;

    unsigned long ms = millis();
    char line[128];
    snprintf(line, sizeof(line), "%lu,%d,%d,%d,%d\n",
             ms, dist1, status1, dist2, status2);

    logFile.print(line);
    logFile.flush();  // ensure data is written (every write = safe but slower)
}

// ────────────────────────────────────────────────
// Endpoint to delete current log file
void handle_delete() {

    if(!server.hasArg("file")){
        server.send(400,"text/plain","missing file");
        return;
    }

    String filename = server.arg("file");

    if(!LittleFS.exists(filename)){
        server.send(404,"text/plain","file not found");
        return;
    }

    if(logFile && filename == current_log_file){
        logFile.close();
        current_log_file = "";
    }

    LittleFS.remove(filename);

    log_info("Deleted file: %s", filename.c_str());

    server.send(200,"text/plain","deleted");
}

// ────────────────────────────────────────────────
// JSON API that lists files in LittleFS
// Response format:
// [
//  {"name":"/log_0.csv","size":10234},
//  {"name":"/log_1.csv","size":54321}
// ]

void handle_list() {

    File root = LittleFS.open("/");
    File file = root.openNextFile();

    String json = "[";
    bool first = true;

    while (file) {
        if (!first) json += ",";
        first = false;

        json += "{";
        json += "\"name\":\"" + String(file.name()) + "\",";
        json += "\"size\":" + String(file.size());
        json += "}";

        file = root.openNextFile();
    }

    json += "]";

    server.send(200, "application/json", json);
}


// ────────────────────────────────────────────────
// File manager web page
void handle_files_page() {

String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>ESP32 File Manager</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{font-family:Arial;background:#f5f5f5;text-align:center}
table{margin:auto;border-collapse:collapse;width:90%}
th,td{border:1px solid #ccc;padding:10px}
th{background:#2c3e50;color:white}
button{padding:6px 12px;margin:2px}
</style>
</head>

<body>

<h2>ESP32 LittleFS Files</h2>

<table>
<thead>
<tr>
<th>File</th>
<th>Size (bytes)</th>
<th>Download</th>
<th>Delete</th>
</tr>
</thead>

<tbody id="fileTable"></tbody>
</table>

<script>

function loadFiles(){

fetch("/list")
.then(r=>r.json())
.then(files=>{

let html="";

files.forEach(f=>{
html+=`
<tr>
<td>${f.name}</td>
<td>${f.size}</td>
<td><a href="/log?file=/${f.name}" target="_blank">Download</a></td>
<td><button onclick="deleteFile('${f.name}')">Delete</button></td>
</tr>
`;
});

document.getElementById("fileTable").innerHTML=html;

});

}

function deleteFile(name){

if(!confirm("Delete "+name+" ?")) return;

fetch("/delete?file=/"+name)
.then(()=>loadFiles());

}

loadFiles();

</script>

</body>
</html>
)rawliteral";

server.send(200,"text/html",html);
}
// -------------------------------------------



// ────────────────────────────────────────────────
// Main setup and loop
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

    // ----------------------------------------------------------
    // WiFi
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);  // Clear previous connections
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);     //SSID, and password from secrets.h
    WiFi.setTxPower(WIFI_POWER_8_5dBm);          // Because of the poor desing of the antenna on the Super mini
    log_info("Connecting to WiFi: %s", WIFI_SSID);


    uint32_t timeout = millis() + 15000;
    while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
        delay(500);
        log_info("WiFi status: %d", WiFi.status());
    }

    if (WiFi.status() == WL_CONNECTED) {
        log_info("WiFi connected – IP: %s", WiFi.localIP().toString().c_str());
        server.on("/", handle_root);              //http://ESP_IP/ returns the dashboard page
        server.on("/data", handle_data);          //http://ESP_IP/data returns JSON with latest sensor readings
        server.on("/log", handle_download);       //http://ESP_IP/log?file=/file_name.csv returns current log file for download
        server.on("/delete", handle_delete);      //http://ESP_IP/delete?file=/file_name.csv deletes specified file
        server.on("/files", handle_files_page);   //http://ESP_IP/files shows file manager page
        server.on("/list", handle_list);          //http://ESP_IP/list returns JSON list of files in LittleFS
    

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

    // Log to CSV and Serial
    log_sensor_data(dist1, m1.RangeStatus, dist2, m2.RangeStatus);
    // Serial log with more detail  (status codes, timestamps)  Note: Also logged in CSV 
    // log_info("S1: %4d mm (status %d) | S2: %4d mm (status %d)", dist1, m1.RangeStatus, dist2, m2.RangeStatus);

    delay(250);  // 4 Hz – comfortable for phone refresh
}