#include <Wire.h>
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(8, 9);
  Serial.println("SCAN");

  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("FOUND 0x%02X\n", a);
    }
  }
}

void loop() {}