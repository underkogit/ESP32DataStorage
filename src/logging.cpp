#include "logging.h"
#include "config.h"
#include "storage.h"

namespace logging {

namespace {
constexpr int CAPACITY = 32;
String buffer[CAPACITY];
int head = 0;
int count = 0;

bool dirEnsured = false;

void ringPut(const String& line) {
    buffer[head] = line;
    head = (head + 1) % CAPACITY;
    if (count < CAPACITY) count++;
}

String mainPath() {
    return String(LOG_DIR) + "/" + LOG_FILE;   // "/logs/host.log"
}

// ensure LOG_DIR exists on the log card (mkdir is idempotent-friendly)
bool ensureDir(Storage* s) {
    if (dirEnsured) return true;
    if (s->fs().mkdir(LOG_DIR)) {
        dirEnsured = true;
    } else {
        fs::File d = s->fs().open(LOG_DIR);
        if (d) { d.close(); dirEnsured = true; }
    }
    return dirEnsured;
}

// Open-append-close of every line: crash-safe, no stale handles when a card
// is pulled while holding a descriptor. Cost of a few ms per line is fine for
// request-rate logging.
void appendToCard(const String& full) {
    Storage* s = (LOG_CARD_INDEX >= 0 && LOG_CARD_INDEX < storage::count())
        ? storage::get(LOG_CARD_INDEX) : nullptr;
    if (!s || !s->ready() || !ensureDir(s)) return;

    String fp = mainPath();
    fs::File f = s->fs().open(fp, FILE_APPEND);
    if (!f) return;

    // rotation: keep host.log bounded, older data moves to host.log.1
    if (LOG_MAX_FILE_BYTES > 0 && (uint32_t)f.size() > LOG_MAX_FILE_BYTES) {
        f.close();
        String old = fp + ".1";
        s->fs().remove(old);            // silent: only the previous generation
        if (s->fs().rename(fp, old)) {
            String msg = "[LOG] rotate " + fp + " -> " + old;
            Serial.println(msg);
            ringPut(msg);
        }
        f = s->fs().open(fp, FILE_APPEND);
        if (!f) return;
    }

    if (f.write((const uint8_t*)full.c_str(), full.length()) != full.length()) return;
    f.write((const uint8_t*)"\n", 1);
    f.close();
}

} // namespace

void put(const String& line) {
    uint32_t ms = millis();
    String stamp = "[+" + String((ms / 1000UL)) + "." + String((ms % 1000UL) / 100UL) + "s] ";
    String full = stamp + line;

    Serial.println(full);
    ringPut(full);
    appendToCard(full);
}

String dump() {
    String out = "[";
    int start = (head - count + CAPACITY) % CAPACITY;
    for (int i = 0; i < count; i++) {
        if (i) out += ',';
        const String& s = buffer[(start + i) % CAPACITY];
        String esc = "";
        for (size_t j = 0; j < s.length(); j++) {
            char c = s[j];
            if (c == '"' || c == '\\') { esc += '\\'; esc += c; }
            else if (c == '\n') esc += "\\n";
            else if (c == '\r') continue;
            else esc += c;
        }
        out += '"' + esc + '"';
    }
    out += "]";
    return out;
}

} // namespace logging
