#pragma once

#include "balance/types.hpp"

#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#else
#include <mutex>
#endif

namespace balance {

class SnapshotStore {
public:
    void publish(const TelemetrySnapshot& snapshot);
    TelemetrySnapshot read() const;

private:
    TelemetrySnapshot snapshot_{};
#ifdef ARDUINO_ARCH_ESP32
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
#else
    mutable std::mutex mutex_;
#endif
};

}
