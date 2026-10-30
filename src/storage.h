#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include "config.h"

class Storage {
public:
    explicit Storage(const StorageDef& def, int index);

    bool begin();
    void end();
    void poll();

    bool ready() const { return _ready; }
    const StorageDef& def() const { return _def; }
    fs::SDFS& fs() { return *_sd; }
    // POSIX vfs mount point (e.g. "/sd0") — used for fast low-level listing
    const char* mount() const { return _mount; }
    bool check();

private:
    const StorageDef& _def;
    const int _index;
    SPIClass* _spi;
    fs::SDFS* _sd;
    bool _ready;
    char _mount[10];
};

namespace storage {
void setup();
void poll();
int count();
Storage* get(int index);
}
