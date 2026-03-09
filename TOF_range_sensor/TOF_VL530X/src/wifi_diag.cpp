#include <Arduino.h>
#include <WiFi.h>

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\nESP32-C3 RF diagnostic");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(1000);

    Serial.println("Starting RF scan...");

    int n = WiFi.scanNetworks();

    Serial.printf("Scan result: %d networks found\n", n);

    for (int i = 0; i < n; i++) {
        Serial.printf("%2d | %-20s | RSSI %4d | CH %2d\n",
            i + 1,
            WiFi.SSID(i).c_str(),
            WiFi.RSSI(i),
            WiFi.channel(i));
    }

    Serial.println("RF diagnostic complete");
}

void loop() {}