#include <Arduino.h>

void setup() {
    pinMode(8, OUTPUT);  // most C3 Super Mini blue LEDs are on GPIO8
    Serial.begin(115200);
    delay(1000);
    Serial.println("BLINK_TEST_START");
}

void loop() {
    digitalWrite(8, HIGH);
    delay(300);
    digitalWrite(8, LOW);
    delay(300);
    Serial.println("Alive");
}