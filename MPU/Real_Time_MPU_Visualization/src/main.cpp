#include <Arduino.h>
#include <Wire.h>
#include <MPU6500_WE.h>

#define SDA_PIN 8
#define SCL_PIN 9

MPU6500_WE myIMU(&Wire, 0x68);

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("MPU6500 START");

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  if (!myIMU.init()) {
    Serial.println("IMU INIT FAILED");
    while (1);
  }

  Serial.println("IMU INIT OK");

  myIMU.setAccRange(MPU9250_ACC_RANGE_8G);
  myIMU.setGyrRange(MPU9250_GYRO_RANGE_500);
  myIMU.setAccDLPF(MPU9250_DLPF_6);
  myIMU.setGyrDLPF(MPU9250_DLPF_6);
}

void loop() {
  static uint32_t lastSend = 0;

  if (micros() - lastSend >= 20000) { // 50 Hz
    lastSend = micros();

    xyzFloat acc = myIMU.getGValues();     // g
    xyzFloat gyr = myIMU.getGyrValues();   // deg/s

    Serial.printf(
      "MPU,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
      acc.x,
      acc.y,
      acc.z,
      gyr.x,
      gyr.y,
      gyr.z
    );
  }
}