#include "balance/wifi_manager.hpp"
#include <cstring>

namespace balance {
namespace {
bool due(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }
uint32_t checksum(const uint8_t* bytes, size_t size) {
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0U);
    }
    return ~crc;
}
void put32(uint8_t* p, uint32_t value) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (i * 8));
}
uint32_t get32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
bool decode(const WifiRecord& record, WifiCredentials& credentials) {
    if (memcmp(record.bytes, "BW01", 4) != 0 || get32(record.bytes + 4) != 1 ||
        get32(record.bytes + 106) != checksum(record.bytes, 106)) return false;
    memcpy(credentials.ssid, record.bytes + 8, sizeof(credentials.ssid));
    memcpy(credentials.password, record.bytes + 41, sizeof(credentials.password));
    if (!validWifiCredentials(credentials)) { clearWifiCredentials(credentials); return false; }
    return true;
}
}
void clearWifiCredentials(WifiCredentials& value) {
    volatile uint8_t* p = reinterpret_cast<volatile uint8_t*>(&value);
    for (size_t i = 0; i < sizeof(value); ++i) p[i] = 0;
}
bool validWifiCredentials(const WifiCredentials& v) {
    const char* ssidEnd = static_cast<const char*>(memchr(v.ssid, 0, sizeof(v.ssid)));
    const char* passEnd = static_cast<const char*>(memchr(v.password, 0, sizeof(v.password)));
    if (!ssidEnd || ssidEnd == v.ssid || !passEnd) return false;
    const size_t length = passEnd - v.password;
    if (length == 0) return true; // Open networks need explicit authorization in the HTTP adapter.
    if (length < 8 || length > 64) return false;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char ch = v.password[i];
        if (ch < 32 || ch > 126) return false;
        if (length == 64 && !((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) return false;
    }
    return true;
}
bool subnetOverlapsAp(uint32_t ip, uint32_t mask) {
    const uint32_t inverse = ~mask;
    if (ip == 0 || mask == 0 || (inverse & (inverse + 1)) != 0) return true;
    return (ip & mask) <= 0xc0a804ffU && (ip | inverse) >= 0xc0a80400U;
}
bool WifiCredentialJournal::load(WifiCredentials& value) {
    uint8_t active = 255;
    WifiRecord record;
    clearWifiCredentials(value);
    readError_ = false;
    // Never fall back to an unselected slot: it may contain an unconfirmed candidate.
    if (!storage_.readActive(active)) { readError_ = true; return false; }
    if (active == 255) return false;
    if (active > 1 || !storage_.readSlot(active, record) || !decode(record, value)) {
        readError_ = true; return false;
    }
    return true;
}
bool WifiCredentialJournal::save(const WifiCredentials& value) {
    if (!validWifiCredentials(value)) return false;
    uint8_t active = 255;
    if (!storage_.readActive(active) || (active > 1 && active != 255)) return false;
    if (active <= 1) {
        WifiRecord previous;
        WifiCredentials decoded;
        const bool valid = storage_.readSlot(active, previous) && decode(previous, decoded);
        clearWifiCredentials(decoded);
        if (!valid) return false; // Unknown/corrupt current state is never overwritten implicitly.
    }
    const uint8_t next = active == 0 ? 1 : 0;
    WifiRecord record;
    memcpy(record.bytes, "BW01", 4);
    put32(record.bytes + 4, 1); // Schema version; reserved for migration.
    memcpy(record.bytes + 8, value.ssid, sizeof(value.ssid));
    memcpy(record.bytes + 41, value.password, sizeof(value.password));
    put32(record.bytes + 106, checksum(record.bytes, 106));
    if (!storage_.writeSlot(next, record)) return false;
    WifiRecord verified;
    if (!storage_.readSlot(next, verified) || memcmp(record.bytes, verified.bytes, sizeof(record.bytes)) != 0) return false;
    return storage_.writeActive(next);
}
const char* wifiPhaseName(WifiPhase value) {
    switch (value) {
    case WifiPhase::Unconfigured: return "unconfigured";
    case WifiPhase::Connecting: return "connecting";
    case WifiPhase::Online: return "online";
    case WifiPhase::Backoff: return "backoff";
    case WifiPhase::Provisioning: return "connecting_and_saving";
    case WifiPhase::Conflict: return "subnet_conflict";
    }
    return "unknown";
}
const char* wifiErrorName(WifiError value) {
    switch (value) {
    case WifiError::None: return "none";
    case WifiError::InvalidCredentials: return "invalid_credentials";
    case WifiError::Timeout: return "connection_timeout";
    case WifiError::LinkLost: return "link_lost";
    case WifiError::Conflict: return "subnet_conflict";
    case WifiError::Dhcp: return "dhcp_failed";
    case WifiError::Authentication: return "authentication_failed";
    case WifiError::Storage: return "storage_failed";
    case WifiError::Cancelled: return "cancelled";
    }
    return "unknown";
}
void WifiManager::begin(uint32_t now) {
    hasSaved_ = journal_.load(saved_);
    if (journal_.readError()) error_ = WifiError::Storage;
    if (hasSaved_) attempt(now);
}
void WifiManager::attempt(uint32_t now) {
    radio_.disconnect();
    if (++epoch_ == 0) ++epoch_; // 0 never identifies a live attempt.
    associated_ = false;
    radio_.connect(trialActive() ? candidate_ : saved_, epoch_);
    if (!trialActive()) phase_ = WifiPhase::Connecting;
    since_ = now;
}
bool WifiManager::connectAndSave(const WifiCredentials& value, uint32_t now) {
    if (trialActive()) return false;
    if (!validWifiCredentials(value)) { error_ = WifiError::InvalidCredentials; return false; }
    // Only this explicit user operation arms persistence; network events never do.
    candidate_ = value;
    phase_ = WifiPhase::Provisioning;
    error_ = WifiError::None;
    result_ = "connecting"; resultError_ = WifiError::None; resultIp_ = 0;
    attempt(now);
    return true;
}
WifiError validateWifiLease(const WifiLease& lease) {
    const auto unicast = [](uint32_t ip) {
        return (ip >> 24) != 0 && (ip >> 24) != 127 && (ip >> 24) < 224 &&
               (ip & 0xffff0000U) != 0xa9fe0000U;
    };
    const uint32_t inverse = ~lease.mask;
    if (!lease.epoch || !unicast(lease.ip) || !unicast(lease.gateway) ||
        !lease.mask || inverse < 3 || (inverse & (inverse + 1)) != 0 ||
        (lease.ip & inverse) == 0 || (lease.ip & inverse) == inverse ||
        (lease.gateway & inverse) == 0 || (lease.gateway & inverse) == inverse ||
        lease.gateway == lease.ip || (lease.ip & lease.mask) != (lease.gateway & lease.mask)) return WifiError::Dhcp;
    return subnetOverlapsAp(lease.ip, lease.mask) ? WifiError::Conflict : WifiError::None;
}
bool WifiManager::gotIp(uint32_t now, const WifiLease& lease) {
    if (lease.epoch != epoch_ || !associated_ ||
        (phase_ != WifiPhase::Provisioning && phase_ != WifiPhase::Connecting)) return false;
    if (static_cast<uint32_t>(now - since_) >= 25000) {
        failed(now, lease.epoch, WifiError::Dhcp); return false;
    }
    const auto error = validateWifiLease(lease);
    if (error != WifiError::None) { failed(now, lease.epoch, error); return false; }
    if (trialActive()) {
        // Synchronous, serialized in the Arduino loop, never in the event callback or PID task.
        if (!journal_.save(candidate_)) { rollback(now, WifiError::Storage); return false; }
        saved_ = candidate_; clearWifiCredentials(candidate_);
        hasSaved_ = true; result_ = "saved"; resultError_ = WifiError::None; resultIp_ = lease.ip;
    }
    phase_ = WifiPhase::Online; error_ = WifiError::None; backoffMs_ = 1000;
    return true;
}
void WifiManager::failed(uint32_t now, uint32_t epoch, WifiError error) {
    if (epoch != epoch_ || (phase_ != WifiPhase::Provisioning && phase_ != WifiPhase::Connecting)) return;
    if (trialActive()) rollback(now, error);
    else if (error == WifiError::Conflict) { radio_.disconnect(); phase_ = WifiPhase::Conflict; error_ = error; }
    else backoff(now, error);
}
void WifiManager::backoff(uint32_t now, WifiError error) {
    radio_.disconnect();
    error_ = error;
    phase_ = WifiPhase::Backoff;
    retryAt_ = now + backoffMs_;
    backoffMs_ = backoffMs_ >= 15000 ? 30000 : backoffMs_ * 2;
}
void WifiManager::rollback(uint32_t now, WifiError error) {
    result_ = error == WifiError::Cancelled ? "cancelled" : "failed"; resultError_ = error;
    clearWifiCredentials(candidate_);
    radio_.disconnect();
    error_ = error;
    if (hasSaved_) { phase_ = WifiPhase::Backoff; retryAt_ = now + 1000; backoffMs_ = 1000; }
    else phase_ = error == WifiError::Conflict ? WifiPhase::Conflict : WifiPhase::Unconfigured;
}
void WifiManager::tick(uint32_t now, bool connected, bool conflict) {
    if (conflict) {
        if (trialActive()) rollback(now, WifiError::Conflict);
        else { radio_.disconnect(); phase_ = WifiPhase::Conflict; error_ = WifiError::Conflict; }
        return;
    }
    switch (phase_) {
    case WifiPhase::Provisioning:
    case WifiPhase::Connecting:
        // A boolean link/status is never sufficient to authorize persistence.
        if (static_cast<uint32_t>(now - since_) >= 25000) {
            failed(now, epoch_, associated_ ? WifiError::Dhcp : WifiError::Timeout);
        }
        break;
    case WifiPhase::Online:
        if (!connected) backoff(now, WifiError::LinkLost);
        break;
    case WifiPhase::Backoff:
        if (due(now, retryAt_)) attempt(now);
        break;
    default: break;
    }
}
void WifiManager::cancel(uint32_t now) { if (trialActive()) rollback(now, WifiError::Cancelled); }
uint32_t WifiManager::retryInMs(uint32_t now) const {
    return phase_ == WifiPhase::Backoff && !due(now, retryAt_) ? retryAt_ - now : 0;
}
}
