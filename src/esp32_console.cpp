#ifdef ARDUINO_ARCH_ESP32
#include "balance/esp32_console.hpp"
#include <Arduino.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>

#ifndef BALANCEBOT_LOG_LEVEL
#define BALANCEBOT_LOG_LEVEL 1
#endif
static_assert(BALANCEBOT_LOG_LEVEL >= 0 && BALANCEBOT_LOG_LEVEL <= 2, "BALANCEBOT_LOG_LEVEL: 0 QUIET, 1 NORMAL, 2 DEBUG");
namespace {
struct Record { balance::LogKind kind; char text[384]; };
QueueHandle_t logs = nullptr;
QueueHandle_t critical = nullptr;
QueueHandle_t lastCritical = nullptr;
std::atomic<balance::LogLevel> level{static_cast<balance::LogLevel>(BALANCEBOT_LOG_LEVEL)};
std::atomic<uint32_t> coalesced{0};
}
void consoleInit() {
    logs = xQueueCreate(12, sizeof(Record));
    critical = xQueueCreate(4, sizeof(Record));
    lastCritical = xQueueCreate(1, sizeof(Record));
    if (!logs || !critical || !lastCritical) Serial.println("[CRITICO] Console sem memoria para filas; use status para estado de seguranca.");
}
balance::LogLevel consoleLevel() { return level.load(std::memory_order_relaxed); }
void consoleSetLevel(balance::LogLevel value) { level.store(value, std::memory_order_relaxed); }
void consoleLog(balance::LogKind kind, const char* format, ...) {
    if (!balance::logAllowed(consoleLevel(), kind)) return;
    Record record{}; record.kind = kind;
    va_list arguments; va_start(arguments, format);
    const int length = vsnprintf(record.text, sizeof(record.text), format, arguments);
    va_end(arguments);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(record.text)) {
        snprintf(record.text, sizeof(record.text), "[CRITICO] Mensagem de console excedeu buffer; consulte status.\n");
        record.kind = balance::LogKind::Critical;
    }
    QueueHandle_t queue = record.kind == balance::LogKind::Critical ? critical : logs;
    if (queue && xQueueSend(queue, &record, 0) == pdTRUE) return;
    if (record.kind == balance::LogKind::Critical) {
        if (lastCritical) xQueueOverwrite(lastCritical, &record);
        coalesced.fetch_add(1, std::memory_order_relaxed);
    }
}
void consoleDrain() { // Only Arduino loop writes UART; control producers never wait for Serial.
    Record record;
    for (unsigned budget = 0; budget < 4; ++budget) {
        if (critical && xQueueReceive(critical, &record, 0) == pdTRUE) Serial.print(record.text);
        else break;
    }
    if (lastCritical && xQueueReceive(lastCritical, &record, 0) == pdTRUE) Serial.print(record.text);
    const uint32_t count = coalesced.exchange(0);
    if (count) Serial.printf("[CRITICO] %lu eventos criticos excederam a fila; ultimo preservado. Consulte status/FAULT.\n", static_cast<unsigned long>(count));
    for (unsigned budget = 0; budget < 4; ++budget) {
        if (!logs || xQueueReceive(logs, &record, 0) != pdTRUE) break;
        if (balance::logAllowed(consoleLevel(), record.kind)) Serial.print(record.text);
    }
}
#endif
