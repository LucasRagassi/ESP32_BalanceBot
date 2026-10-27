#pragma once
#include <cstdint>

namespace balance {
enum class ApEvent : uint8_t { Unknown, Start, Stop };
const char* apEventName(ApEvent event);
struct ApObservation {
    ApEvent last = ApEvent::Unknown;
    bool started = false;
    uint32_t atMs = 0;
    uint32_t starts = 0;
    uint32_t stops = 0;
    uint32_t joins = 0;
    uint32_t leaves = 0;
};
// Pure observer: no start/stop/recovery actions. Adapter serializes event access.
void observeAp(ApObservation& observation, ApEvent event, uint32_t now);
struct ApDriverEvidence {
    bool modeOk = false;
    bool configOk = false;
    bool channelOk = false;
    bool clientsOk = false;
    bool interfaceUp = false;
    bool ipAssigned = false;
    bool ssidHidden = true;
    bool ssidNonempty = false;
    bool ssidMatchesExpected = false;
    unsigned mode = 0;
    unsigned channel = 0;
};
// Reports software consistency, never proof that RF beacons are received externally.
bool apDriverReady(const ApObservation& observation, const ApDriverEvidence& driver);

enum class ScanState : uint8_t { Never, Running, Complete, Failed };
const char* scanStateName(ScanState state);
class ExplicitScan {
public:
    bool allowed(uint32_t now, bool busy, bool apReady) const;
    void started(uint32_t now, int result);
    void finished(uint32_t now, int result);
    bool running() const { return state_ == ScanState::Running; }
    ScanState state() const { return state_; }
    uint32_t starts() const { return starts_; }
    uint32_t startMs() const { return startMs_; }
    uint32_t endMs() const { return endMs_; }
    int result() const { return result_; }
private:
    ScanState state_ = ScanState::Never;
    uint32_t starts_ = 0;
    uint32_t startMs_ = 0;
    uint32_t endMs_ = 0;
    int result_ = -2;
};
}
