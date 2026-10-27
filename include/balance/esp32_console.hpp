#pragma once
#include "balance/console.hpp"
#ifdef ARDUINO_ARCH_ESP32
void consoleInit();
balance::LogLevel consoleLevel();
void consoleSetLevel(balance::LogLevel level);
void consoleLog(balance::LogKind kind, const char* format, ...)
    __attribute__((format(printf, 2, 3)));
void consoleDrain();
inline void consoleLine(balance::LogKind kind, const char* text) { consoleLog(kind, "%s\n", text); }
#endif
