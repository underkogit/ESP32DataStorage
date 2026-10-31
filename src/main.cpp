#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "storage.h"
#include "web.h"
#include "logging.h"

static const char* wlStatusName(int s) {
    switch (s) {
        case WL_IDLE_STATUS:    return "IDLE";
        case WL_NO_SSID_AVAIL:  return "NO_SSID (сети нет в эфире)";
        case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
        case WL_CONNECTED:      return "CONNECTED";
        case WL_CONNECT_FAILED: return "CONNECT_FAILED (пароль / режим защиты)";
        case WL_CONNECTION_LOST:return "CONNECTION_LOST";
        case WL_DISCONNECTED:   return "DISCONNECTED";
        default:                return "UNKNOWN";
    }
}

void startWiFi() {
    if (strlen(WIFI_SSID) > 0) {
        // пароль в лог не печатаем — только его длину (ловит лишние пробелы в конфиге)
        logging::put(String("[WIFI] STA: подключаюсь к '") + WIFI_SSID +
                     "' (пароль длина=" + String((int)strlen(WIFI_PASSWORD)) + ")");

        if (WIFI_SCAN_DEBUG) {
            WiFi.mode(WIFI_STA);
            int n = WiFi.scanNetworks();
            bool seen = false;
            if (n <= 0) {
                logging::put("[WIFI] scan: эфир пуст — роутер далеко или выключен");
            } else {
                for (int i = 0; i < n; i++) {
                    logging::put(String("[WIFI] scan: '") + WiFi.SSID(i) +
                                 "' ch=" + String(WiFi.channel(i)) +
                                 " " + String(WiFi.RSSI(i)) + " dBm auth=" +
                                 String((int)WiFi.encryptionType(i)));
                    if (WiFi.SSID(i) == WIFI_SSID) seen = true;
                }
                if (seen) {
                    logging::put(String("[WIFI] scan: '") + WIFI_SSID +
                                 "' найдена → дело в пароле или настройках защиты роутера");
                } else {
                    logging::put(String("[WIFI] scan: '") + WIFI_SSID +
                                 "' в эфире НЕТ → роутер только 5 ГГц, скрытая сеть или далеко");
                }
            }
        }

        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_STA_TIMEOUT_MS) {
            delay(100);
        }

        if (WiFi.status() == WL_CONNECTED) {
            WiFi.setSleep(false); // modem sleep kills HTTP throughput on many routers
            logging::put("[WIFI] connected, IP: " + WiFi.localIP().toString() +
                         ", ch=" + String(WiFi.channel()) +
                         ", RSSI: " + String(WiFi.RSSI()) + " dBm");
            return;
        }
        logging::put(String("[WIFI] STA connect failed (timeout ") +
                     String((unsigned)WIFI_STA_TIMEOUT_MS) + " ms, статус: " +
                     wlStatusName(WiFi.status()) + ")");
    } else {
        logging::put("[WIFI] STA not configured");
    }

    // Fallback: собственная точка доступа
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    logging::put("[WIFI] AP \"" + String(AP_SSID) + "\" включена, IP: " + WiFi.softAPIP().toString());
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    logging::put("--- ESP32 Data Storage ---");

    SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);

    storage::setup(); // лог-карта готова: всё после этого попадает ещё и в /logs/host.log
    startWiFi();      // MUST be before creating WebServer: lwIP must be started
    web::begin();
}

void loop() {
    web::handle();
    storage::poll();
}
