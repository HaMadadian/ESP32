#include <WiFi.h>
#include <Arduino.h>

const char* ssid = "Odido-1E84C1";
const char* password = "8DQ37UDWLH79KW4M";

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("Starting WiFi test");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(1000);

    Serial.println("Connecting...");
    WiFi.begin(ssid, password);

    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    int attempt = 0;

    while (WiFi.status() != WL_CONNECTED && attempt < 30) {
        Serial.printf("Status: %d\n", WiFi.status());
        delay(500);
        attempt++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected!");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("Connection FAILED");
    }
}

void loop() {}