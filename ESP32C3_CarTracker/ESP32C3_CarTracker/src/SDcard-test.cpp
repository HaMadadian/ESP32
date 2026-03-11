#include <SD.h>
#include <SPI.h>

#define SD_CS_PIN 0   // change to your new CS pin

void setup() {
    Serial.begin(115200);
    delay(3000);
    Serial.println("SD Minimal Test");

    SPI.begin(10, 3, 2, SD_CS_PIN);  // SCK, MISO, MOSI, CS

    Serial.println("SPI initialized with pins");

    if (!SD.begin(SD_CS_PIN, SPI, 2000000UL)) {
        Serial.println("SD.begin() FAILED");
    } else {
        Serial.println("SD OK");
        Serial.printf("Card size: %llu bytes (~%llu MB)\n", SD.cardSize(), SD.cardSize() / (1024*1024));
    }
}

void loop() {
    delay(1000);
}