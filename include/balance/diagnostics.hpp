#pragma once

#include <cstddef>
#include <cstdint>

namespace balance {

constexpr size_t kHealthJsonCapacity = 3072;
constexpr uint32_t kControlHeartbeatMaxAgeMs = 2000;

// snprintf returns the required length, excluding NUL; equality is truncation.
constexpr bool formatFits(int result, size_t capacity) {
    return result >= 0 && static_cast<size_t>(result) < capacity;
}

bool heartbeatFresh(bool seen, uint32_t nowMs, uint32_t publishedAtMs);

struct HealthDiagnostics {
    const char* version = "unknown";
    const char* commit = "unknown";
    const char* treeState = "unknown";
    const char* sourceSha256 = "unknown";
    const char* buildUtc = "unknown";
    const char* mode = "DEMO";
    const char* resetReason = "unknown";
    const char* ssid = "";
    const char* ip = "0.0.0.0";
    const char* gateway = "0.0.0.0";
    const char* netmask = "0.0.0.0";
    const char* staSsid = "";
    const char* staIp = "0.0.0.0";
    const char* staGateway = "0.0.0.0";
    const char* staNetmask = "0.0.0.0";
    const char* staState = "unconfigured";
    const char* staError = "none";
    int staRssi = 0;
    unsigned staChannel = 0;
    unsigned apChannel = 0;
    unsigned staDisconnectReason = 0;
    bool staConnected = false;
    bool mdnsReady = false;
    const char* wifiDriverMode = "UNKNOWN";
    const char* apLastEvent = "UNKNOWN";
    const char* apBssid = "";
    const char* scanState = "never";
    int apModeResult = -1;
    int apConfigResult = -1;
    uint32_t apEventMs = 0;
    uint32_t apStarts = 0;
    uint32_t apStops = 0;
    uint32_t scanStarts = 0;
    uint32_t uptimeMs = 0;
    uint32_t freeHeap = 0;
    uint32_t minFreeHeap = 0;
    uint32_t heartbeatAtMs = 0;
    uint32_t heartbeatCount = 0;
    uint32_t httpRequests = 0;
    uint32_t wsConnections = 0;
    unsigned wsClients = 0;
    unsigned apClients = 0;
    unsigned state = 0;
    unsigned fault = 0;
    bool apConfigReady = false;
    bool apReady = false;
    bool httpListening = false;
    bool websocketListening = false;
    bool littleFsReady = false;
    bool indexReady = false;
    bool scriptReady = false;
    bool styleReady = false;
    bool controlTaskCreated = false;
    bool heartbeatSeen = false;
    bool commandQueueReady = false;
    bool configQueueReady = false;
    bool imuAvailable = false;
};

bool healthOk(const HealthDiagnostics& data);
// Failure clears the output: callers must send a complete error response instead.
bool renderHealthJson(const HealthDiagnostics& data, char* output, size_t capacity);

}
