#include "balance/snapshot_store.hpp"

namespace balance {

void SnapshotStore::publish(const TelemetrySnapshot& snapshot) {
#ifdef ARDUINO_ARCH_ESP32
    portENTER_CRITICAL(&mux_);
    snapshot_ = snapshot;
    portEXIT_CRITICAL(&mux_);
#else
    const std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = snapshot;
#endif
}

TelemetrySnapshot SnapshotStore::read() const {
    TelemetrySnapshot result;
#ifdef ARDUINO_ARCH_ESP32
    portENTER_CRITICAL(&mux_);
    result = snapshot_;
    portEXIT_CRITICAL(&mux_);
#else
    const std::lock_guard<std::mutex> lock(mutex_);
    result = snapshot_;
#endif
    return result;
}

}
