#include "balance/wifi_observer.hpp"

namespace balance {
const char* apEventName(ApEvent event) {
    switch (event) { case ApEvent::Unknown: return "UNKNOWN"; case ApEvent::Start: return "AP_START"; case ApEvent::Stop: return "AP_STOP"; }
    return "UNKNOWN";
}
void observeAp(ApObservation& observation, ApEvent event, uint32_t now) {
    if (event == ApEvent::Unknown) return;
    observation.last = event; observation.atMs = now;
    observation.started = event == ApEvent::Start;
    if (observation.started) ++observation.starts; else ++observation.stops;
}
bool apDriverReady(const ApObservation& event, const ApDriverEvidence& d) {
    return event.started && d.modeOk && (d.mode & 2U) != 0 && d.configOk && d.channelOk &&
           d.channel >= 1 && d.channel <= 14 && d.clientsOk && d.interfaceUp && d.ipAssigned &&
           d.ssidNonempty && d.ssidMatchesExpected && !d.ssidHidden;
}
const char* scanStateName(ScanState state) {
    switch (state) { case ScanState::Never: return "never"; case ScanState::Running: return "running";
    case ScanState::Complete: return "complete"; case ScanState::Failed: return "failed"; }
    return "unknown";
}
bool ExplicitScan::allowed(uint32_t now, bool busy, bool apReady) const {
    return apReady && !busy && !running() && (starts_ == 0 || static_cast<uint32_t>(now - startMs_) >= 15000);
}
void ExplicitScan::started(uint32_t now, int result) {
    ++starts_; startMs_ = now; result_ = result;
    state_ = result == -1 || result >= 0 ? ScanState::Running : ScanState::Failed;
    if (!running()) endMs_ = now;
}
void ExplicitScan::finished(uint32_t now, int result) {
    endMs_ = now; result_ = result;
    state_ = result >= 0 ? ScanState::Complete : ScanState::Failed;
}
}
