#include "logging.h"

namespace logging {

namespace {
constexpr int CAPACITY = 32;
String buffer[CAPACITY];
int head = 0;
int count = 0;
}

void put(const String& line) {
    buffer[head] = line;
    head = (head + 1) % CAPACITY;
    if (count < CAPACITY) count++;
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
