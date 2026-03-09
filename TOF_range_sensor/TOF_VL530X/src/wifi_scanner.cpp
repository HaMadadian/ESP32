#include <WiFi.h>

void setup() {
    Serial.begin(115200);
    delay(1000);

    WiFi.mode(WIFI_STA);        // station mode
    WiFi.disconnect(true);      // clear previous connections
    delay(100);

    Serial.println("Scanning for Wi-Fi networks...");
    int n = WiFi.scanNetworks();   // scan networks
    Serial.printf("Networks found: %d\n", n);

    for (int i = 0; i < n; i++) {
        Serial.printf("%d: SSID: %s | RSSI: %d | Channel: %d | Encryption: %s\n",
                      i + 1,
                      WiFi.SSID(i).c_str(),
                      WiFi.RSSI(i),
                      WiFi.channel(i),
                      (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "SECURE"));
    }
}

void loop() {
    // nothing
}