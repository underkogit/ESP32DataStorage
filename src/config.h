#pragma once

#include <stdint.h>
#include <stddef.h>

struct StorageDef {
    const char* name;
    uint8_t cs;
    uint8_t sck;
    uint8_t miso;
    uint8_t mosi;
};

extern const StorageDef STORAGE_CARDS[];
extern const size_t STORAGE_CARD_COUNT;

extern const char* WIFI_SSID;
extern const char* WIFI_PASSWORD;
extern const char* AP_SSID;
extern const char* AP_PASSWORD;

extern const uint32_t WIFI_STA_TIMEOUT_MS;

#define SPI_SCK_PIN 18
#define SPI_MISO_PIN 19
#define SPI_MOSI_PIN 23
