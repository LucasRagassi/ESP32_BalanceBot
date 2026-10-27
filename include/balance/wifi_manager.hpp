#pragma once
#include <cstddef>
#include <cstdint>

namespace balance {
struct WifiCredentials {
    char ssid[33]{};
    char password[65]{};
};
bool validWifiCredentials(const WifiCredentials& value);
bool subnetOverlapsAp(uint32_t ip, uint32_t mask); // IPv4 in canonical host order.
constexpr bool is24GhzChannel(int channel) { return channel >= 1 && channel <= 14; }
void clearWifiCredentials(WifiCredentials& value);

// Fixed, initialized wire record: no compiler-dependent padding or integer layout.
struct WifiRecord { uint8_t bytes[110]{}; };
class WifiSlotStorage {
public:
    virtual ~WifiSlotStorage() = default;
    virtual bool readSlot(uint8_t slot, WifiRecord& record) = 0;
    virtual bool writeSlot(uint8_t slot, const WifiRecord& record) = 0;
    virtual bool readActive(uint8_t& slot) = 0;
    // Atomic selector update; failed operation must preserve the previous selector.
    virtual bool writeActive(uint8_t slot) = 0;
};
class WifiCredentialJournal {
public:
    explicit WifiCredentialJournal(WifiSlotStorage& storage) : storage_(storage) {}
    bool load(WifiCredentials& value);
    bool save(const WifiCredentials& value);
    bool readError() const { return readError_; }
private:
    WifiSlotStorage& storage_;
    bool readError_ = false;
};
class WifiRadio {
public:
    virtual ~WifiRadio() = default;
    virtual void connect(const WifiCredentials& credentials, uint32_t epoch) = 0;
    virtual void disconnect() = 0;
};
enum class WifiPhase : uint8_t { Unconfigured, Connecting, Online, Backoff, Provisioning, Conflict };
enum class WifiError : uint8_t { None, InvalidCredentials, Timeout, LinkLost, Conflict, Dhcp, Authentication, Storage, Cancelled };
const char* wifiPhaseName(WifiPhase value);
const char* wifiErrorName(WifiError value);

struct WifiLease {
    uint32_t epoch = 0, ip = 0, mask = 0, gateway = 0; // canonical host order
};
WifiError validateWifiLease(const WifiLease& lease);

class WifiManager {
public:
    WifiManager(WifiRadio& radio, WifiCredentialJournal& journal) : radio_(radio), journal_(journal) {}
    void begin(uint32_t now);
    bool connectAndSave(const WifiCredentials& value, uint32_t now);
    bool gotIp(uint32_t now, const WifiLease& lease);
    void failed(uint32_t now, uint32_t epoch, WifiError error);
    void associated(uint32_t epoch) { if (epoch == epoch_) associated_ = true; }
    void tick(uint32_t now, bool connected, bool conflict);
    void cancel(uint32_t now);
    WifiPhase phase() const { return phase_; }
    WifiError error() const { return error_; }
    bool trialActive() const { return phase_ == WifiPhase::Provisioning; }
    bool hasSaved() const { return hasSaved_; }
    const char* savedSsid() const { return saved_.ssid; }
    uint32_t epoch() const { return epoch_; }
    const char* result() const { return result_; }
    WifiError resultError() const { return resultError_; }
    uint32_t resultIp() const { return resultIp_; }
    uint32_t retryInMs(uint32_t now) const;
private:
    void attempt(uint32_t now);
    void rollback(uint32_t now, WifiError error);
    void backoff(uint32_t now, WifiError error);
    WifiRadio& radio_;
    WifiCredentialJournal& journal_;
    WifiCredentials saved_{};
    WifiCredentials candidate_{};
    WifiPhase phase_ = WifiPhase::Unconfigured;
    WifiError error_ = WifiError::None;
    bool hasSaved_ = false;
    uint32_t epoch_ = 0;
    bool associated_ = false;
    const char* result_ = "idle";
    WifiError resultError_ = WifiError::None;
    uint32_t resultIp_ = 0;
    uint32_t since_ = 0;
    uint32_t retryAt_ = 0;
    uint32_t backoffMs_ = 1000;
};
}
