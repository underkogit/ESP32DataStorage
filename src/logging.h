#pragma once

#include <Arduino.h>

namespace logging {
void put(const String& line);
String dump();
}
