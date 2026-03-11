#include <Arduino.h>

void setup() {
    // Give USB CDC full time to enumerate on Windows
    for (int i = 0; i < 5; i++) {
        delay(1000);
        if (Serial) break;  // exit early if CDC connects
    }

    Serial.begin(115200);
    Serial.setDebugOutput(true);

    Serial.println("\n\nESP32-C3 Super Mini – Forced Delay Test v4");
    Serial.println("If you see this line → USB CDC sync succeeded");
    Serial.println("Boot reason: " + String(esp_reset_reason()));
    Serial.println("Free heap: " + String(ESP.getFreeHeap()));
}

void loop() {
    static uint32_t last = 0;
    if (millis() - last > 2000) {
        last = millis();
        Serial.printf("Alive – millis: %lu   Free heap: %u\n", millis(), ESP.getFreeHeap());
        pinMode(8, OUTPUT);
        digitalWrite(8, !digitalRead(8));
    }
}