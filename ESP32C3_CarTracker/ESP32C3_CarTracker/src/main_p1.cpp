#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <SD.h>
#include <SPI.h>

// ────────────────────────────────────────────────
// Pins for ESP32-C3 Super Mini
// ────────────────────────────────────────────────
#define GPS_RX_PIN  4     // GPS TX → GPIO4 (UART1 RX)
#define GPS_TX_PIN  5     // GPS RX ← GPIO5 (UART1 TX)
#define I2C_SDA     8
#define I2C_SCL     9

// SD card pins (your confirmed wiring)
#define SD_CS_PIN   7     // GPIO7 = CS
#define SD_MOSI     2     // GPIO2 = MOSI
#define SD_MISO     3     // GPIO3 = MISO
#define SD_SCK     10     // GPIO10 = SCK

// ────────────────────────────────────────────────
// Log file on SD card
// ────────────────────────────────────────────────
const char* LOG_FILE = "/tracker.log";
static File logFile;
static unsigned long last_flush_ms = 0;
const unsigned long FLUSH_INTERVAL_MS = 30000; // flush every 30 s

// ────────────────────────────────────────────────
// Objects
// ────────────────────────────────────────────────
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);        // UART1
Adafruit_MPU6050 mpu;

// Dead reckoning state (your original simple version – untouched)
static float estimated_lat = 52.0f;
static float estimated_lon = 4.0f;
static float velocity_m_s = 0.0f;

// ────────────────────────────────────────────────
// Timestamped logging – prints to serial + saves to SD
// ────────────────────────────────────────────────
void log_msg(const char* level, const char* format, ...) {
    char msg[160];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    unsigned long ms = millis();
    char ts[32];
    snprintf(ts, sizeof(ts), "[%07lu.%03lu] %s: ", ms/1000, ms%1000, level);

    // Serial output
    Serial.print(ts);
    Serial.println(msg);

    // SD append (if file is open)
    if (logFile) {
        logFile.print(ts);
        logFile.println(msg);
    }
}

#define LOG_INFO(...)  log_msg("INFO",  __VA_ARGS__)
#define LOG_WARN(...)  log_msg("WARN",  __VA_ARGS__)
#define LOG_ERROR(...) log_msg("ERROR", __VA_ARGS__)

// ────────────────────────────────────────────────
// Flush SD logs periodically
// ────────────────────────────────────────────────
void flush_logs() {
    if (logFile) {
        logFile.flush();
        last_flush_ms = millis();
        LOG_INFO("Logs flushed to SD card");
    }
}

// ────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1000);
    LOG_INFO("ESP32-C3 Car Tracker Phase 1 starting");

    // SD card – explicit SPI configuration with your exact pins
    LOG_INFO("Initializing SD card with pins: CS=%d, MOSI=%d, MISO=%d, SCK=%d",
             SD_CS_PIN, SD_MOSI, SD_MISO, SD_SCK);

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS_PIN);

    bool sd_ok = false;
    for (int retry = 0; retry < 5; retry++) {
        delay(200);  // extra power-up delay for SD card
        if (SD.begin(SD_CS_PIN, SPI, 2000000UL)) {  // 2 MHz safe init speed
            sd_ok = true;
            LOG_INFO("SD card initialized successfully (try %d)", retry+1);
            LOG_INFO("Card size: %llu bytes (~%llu MB)", SD.cardSize(), SD.cardSize() / (1024*1024));
            LOG_INFO("Card type: %d", SD.cardType());
            break;
        }
        LOG_WARN("SD init attempt %d failed – retrying in 500 ms...", retry+1);
        delay(500);
    }

    if (!sd_ok) {
        LOG_ERROR("SD card init failed after 5 retries");
        LOG_ERROR("Possible causes:");
        LOG_ERROR(" - CS pin conflict (GPIO1 is strapping pin – consider moving CS to GPIO7)");
        LOG_ERROR(" - Bad contact / card not fully inserted");
        LOG_ERROR(" - Power instability (add 100–470 µF cap near SD VCC)");
        LOG_ERROR(" - Card not formatted as FAT32");
    } else {
        // Open or create log file
        logFile = SD.open(LOG_FILE, FILE_APPEND);
        if (!logFile) {
            LOG_ERROR("Failed to open/create %s on SD card", LOG_FILE);
        } else {
            LOG_INFO("Log file opened/created on SD: %s", LOG_FILE);
        }
    }

    // GPS on Hardware UART1
    gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    LOG_INFO("GPS started on UART1 (GPIO4/5)");

    // MPU-9250 on I2C
    Wire.begin(I2C_SDA, I2C_SCL);
    if (!mpu.begin()) {
        LOG_ERROR("MPU9250 failed to initialize!");
    } else {
        LOG_INFO("MPU9250 initialized successfully");
        mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
        mpu.setGyroRange(MPU6050_RANGE_500_DEG);
        mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    }

    // First flush after init
    flush_logs();
}

// ────────────────────────────────────────────────
void loop() {
    static unsigned long last_print = 0;
    static unsigned long last_flush = 0;

    uint32_t now = millis();

    // Read GPS
    while (gpsSerial.available()) {
        char c = gpsSerial.read();
        gps.encode(c);
    }

    // Read MPU-9250
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    // Very simple dead reckoning (demo only - will drift)
    velocity_m_s += a.acceleration.x * 0.05f;

    if (gps.location.isValid()) {
        estimated_lat = gps.location.lat();
        estimated_lon = gps.location.lng();
        velocity_m_s = gps.speed.mps();     // reset when GPS good
    }

    // Log every 1 second
    if (now - last_print > 1000) {
        last_print = now;

        if (gps.location.isValid()) {
            LOG_INFO("GPS FIX | Lat: %.6f | Lon: %.6f | Sats: %d | HDOP: %.2f | Vel: %.2f m/s",
                     gps.location.lat(), gps.location.lng(),
                     gps.satellites.value(), gps.hdop.hdop(),
                     gps.speed.mps());
        } else {
            LOG_INFO("GPS NO FIX | Sats in view: %d", gps.satellites.value());
        }

        LOG_INFO("IMU | Ax: %.3f Ay: %.3f Az: %.3f | Gx: %.3f Gy: %.3f Gz: %.3f",
                 a.acceleration.x, a.acceleration.y, a.acceleration.z,
                 g.gyro.x, g.gyro.y, g.gyro.z);

        LOG_INFO("DEAD RECKONING | Est Lat: %.6f | Lon: %.6f | Vel: %.2f m/s",
                 estimated_lat, estimated_lon, velocity_m_s);
    }

    // Flush SD every 30 seconds
    if (now - last_flush >= FLUSH_INTERVAL_MS) {
        flush_logs();
        last_flush = now;
    }
}