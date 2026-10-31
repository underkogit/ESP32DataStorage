#include "storage.h"
#include "logging.h"
#include "vfs_api.h"

#include <vector>

Storage::Storage(const StorageDef& def, int index)
    : _def(def), _index(index), _spi(nullptr),
      _sd(new fs::SDFS(fs::FSImplPtr(new VFSImpl()))), _ready(false) {
    snprintf(_mount, sizeof(_mount), "/sd%d", _index);
    _mount[sizeof(_mount) - 1] = '\0';
}

bool Storage::begin() {
    if (_ready) return true;
    if (!_spi) _spi = &SPI;

    logging::put(String("[STORAGE] ") + _def.name + ": initializing (CS " + String((unsigned)_def.cs) + ")");
    delay(250);

    if (!_sd->begin(_def.cs, *_spi, 4000000U, _mount)) {
        logging::put(String("[STORAGE] ") + _def.name + ": SDFS.begin failed (mount " + _mount + ")");
        return false;
    }
    _ready = true;
    logging::put(String("[STORAGE] ") + _def.name + ": mounted (" + _mount + ")");
    return true;
}

void Storage::end() {
    if (!_sd) return;
    if (_ready) {
        _sd->end();
        _ready = false;
    }
}

bool Storage::check() {
    if (!_ready) return false;
    if (_sd->cardType() == CARD_NONE) {
        logging::put(String("[STORAGE] ") + _def.name + ": card removed");
        end();
        return false;
    }
    return true;
}
void Storage::poll() {
    if (_ready) check();
}

namespace storage {

static std::vector<Storage*> cards;
static unsigned long lastPoll = 0;
static const unsigned long pollInterval = 1000;

void setup() {
    for (size_t i = 0; i < STORAGE_CARD_COUNT; i++) {
        Storage* s = new Storage(STORAGE_CARDS[i], static_cast<int>(i));
        s->begin();
        cards.push_back(s);
    }
    logging::put("[STORAGE] initialized (" + String((int)cards.size()) + " cards)");
}

void poll() {
    unsigned long now = millis();
    if (now - lastPoll < pollInterval) return;
    lastPoll = now;

    for (Storage* s : cards) {
        bool wasReady = s->ready();
        s->poll();
        if (wasReady && !s->ready()) {
            logging::put(String("[STORAGE] ") + s->def().name + ": unmounted by poll");
        }
    }
}

int count() {
    return static_cast<int>(cards.size());
}

Storage* get(int index) {
    if (index < 0 || index >= static_cast<int>(cards.size())) return nullptr;
    return cards[index];
}

} // namespace storage
