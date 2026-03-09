#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>

// -----------------------------
// Pin configuration
// -----------------------------
#define SDA_PIN   8
#define SCL_PIN   9
#define XSHUT1    10
#define XSHUT2    0

// -----------------------------
Adafruit_VL53L0X sensor1;
Adafruit_VL53L0X sensor2;

// -----------------------------
void scan_i2c()
{
    Serial.println("\n[I2C] Scanning bus...");

    int found = 0;

    for (uint8_t addr = 1; addr < 127; addr++)
    {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0)
        {
            Serial.printf("[I2C] Device found at 0x%02X\n", addr);
            found++;
        }
    }

    if (found == 0)
        Serial.println("[I2C] No devices detected!");
    else
        Serial.printf("[I2C] Scan complete, %d device(s) found\n", found);
}

// -----------------------------
void setup()
{
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n=================================");
    Serial.println("ESP32-C3 VL53L0X SENSOR TEST");
    Serial.println("=================================");

    // -----------------------------
    // I2C INIT
    // -----------------------------
    Serial.println("[INIT] Starting I2C bus");

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);

    Serial.printf("[INIT] SDA = GPIO %d\n", SDA_PIN);
    Serial.printf("[INIT] SCL = GPIO %d\n", SCL_PIN);

    scan_i2c();

    // -----------------------------
    // Configure XSHUT pins
    // -----------------------------
    Serial.println("\n[INIT] Configuring XSHUT pins");

    pinMode(XSHUT1, OUTPUT);
    pinMode(XSHUT2, OUTPUT);

    digitalWrite(XSHUT1, LOW);
    digitalWrite(XSHUT2, LOW);

    Serial.println("[INIT] Both sensors held in reset");
    delay(100);

    // -----------------------------
    // SENSOR 1 START
    // -----------------------------
    Serial.println("\n[SENSOR1] Powering sensor 1");

    digitalWrite(XSHUT1, HIGH);
    delay(150);

    scan_i2c();

    Serial.println("[SENSOR1] Initializing...");

    if (!sensor1.begin())
    {
        Serial.println("[ERROR] Sensor 1 initialization FAILED");
        while (1);
    }

    // Start continuous measurements for sensor1
    sensor1.startRangeContinuous();

    Serial.println("[SENSOR1] Initialization SUCCESS");


    // -----------------------------
    // SENSOR 2 START
    // -----------------------------
    // change address of sensor1
    sensor1.setAddress(0x30);
    Serial.println("Sensor1 address -> 0x30");
    Serial.println("\n[SENSOR2] Powering sensor 2");

    digitalWrite(XSHUT2, HIGH);
    delay(150);

    scan_i2c();

    Serial.println("[SENSOR2] Initializing...");

    if (!sensor2.begin())
    {
        Serial.println("[ERROR] Sensor 2 initialization FAILED");
        while (1);
    }

    // Start continuous measurements for sensor2
    sensor2.startRangeContinuous();


    Serial.println("[SENSOR2] Initialization SUCCESS");

    Serial.println("[SENSOR2] Changing I2C address -> 0x2A");
    sensor2.setAddress(0x2A);

    scan_i2c();

    Serial.println("\n[SYSTEM] Both sensors ready");
}

// -----------------------------
void loop()
{
    VL53L0X_RangingMeasurementData_t m1, m2;

    sensor1.getRangingMeasurement(&m1, false);
    sensor2.getRangingMeasurement(&m2, false);

    Serial.printf(
        "[DATA] S1: %4d mm (status %d) | S2: %4d mm (status %d)\n",
        m1.RangeMilliMeter,
        m1.RangeStatus,
        m2.RangeMilliMeter,
        m2.RangeStatus
    );

    delay(1000);
}