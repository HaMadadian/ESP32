#include <Wire.h>
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(8, 9);

  Wire.beginTransmission(0x68);
  Wire.write(0x75);          // WHO_AM_I
  Wire.endTransmission(false);

  Wire.requestFrom(0x68, 1);
  uint8_t id = Wire.read();

  Serial.printf("WHO_AM_I = 0x%02X\n", id);
}

void loop() {}