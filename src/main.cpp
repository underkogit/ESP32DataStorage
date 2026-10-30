#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "storage.h"
#include "web.h"

void startWiFi() {
    if (strlen(WIFI_SSID) > 0) {
        Serial.println("[WIFI] connecting to " + String(WIFI_SSID) + "...");
        Serial.flush();
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_STA_TIMEOUT_MS) {
            delay(100);
        }

        if (WiFi.status() == WL_CONNECTED) {
            WiFi.setSleep(false); // modem sleep kills HTTP throughput on many routers
            Serial.println("[WIFI] connected, IP: " + WiFi.localIP().toString() +
                           ", RSSI: " + String(WiFi.RSSI()) + " dBm");
            Serial.flush();
            return;
        }
        Serial.println("[WIFI] STA connect failed (timeout " + String((unsigned)WIFI_STA_TIMEOUT_MS) + " ms)");
        Serial.flush();
    } else {
        Serial.println("[WIFI] STA not configured");
        Serial.flush();
    }

    // Fallback: own access point
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.println("[WIFI] AP \"" + String(AP_SSID) + "\" started, IP: " + WiFi.softAPIP().toString());
    Serial.flush();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("--- ESP32 Data Storage ---");
    Serial.flush();

    SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);

    storage::setup();
    startWiFi();      // MUST be before creating WebServer: lwIP must be started
    web::begin();
}

void loop() {
    web::handle();
    storage::poll();
}
