#ifdef ARDUINO_ARCH_ESP32
#include "balance/esp32_network.hpp"
#include "balance/wifi_manager.hpp"
#include "balance/wifi_observer.hpp"
#include "balance/esp32_console.hpp"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_netif.h>
#include <lwip/inet.h>
#include <nvs.h>
#include <atomic>
#include <cstring>

#ifndef BALANCEBOT_AP_SSID
#define BALANCEBOT_AP_SSID "BalanceBot-Setup"
#endif

#ifndef BALANCEBOT_ENABLE_MDNS
#define BALANCEBOT_ENABLE_MDNS 0
#endif
#if BALANCEBOT_ENABLE_MDNS
#include <ESPmDNS.h>
#endif

namespace {
constexpr const char* kNamespace = "bb_wifi";
WebServer* http = nullptr;
void (*invalidate)() = nullptr;
char trialId[33]{};
std::atomic<uint32_t> events{0};
std::atomic<unsigned> disconnectReason{0};
// SDK events have no transaction ID: gate association against the live driver,
// then capture our epoch with the lease. The loop alone may commit NVS.
portMUX_TYPE leaseMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> activeEpoch{0}, associatedEpoch{0};
balance::WifiLease pendingLease;
bool leasePending = false;
uint8_t associatedBssid[6]{};
uint32_t failedEpoch = 0;
balance::WifiError pendingFailure = balance::WifiError::Authentication;
void reportFailure(uint32_t epoch, balance::WifiError error) {
    portENTER_CRITICAL(&leaseMux);
    failedEpoch = epoch; pendingFailure = error;
    portEXIT_CRITICAL(&leaseMux);
}
std::atomic<bool> leaseReady{false};
char expectedSsid[33]{};
balance::ExplicitScan scanSession;
portMUX_TYPE apObservationMux = portMUX_INITIALIZER_UNLOCKED;
balance::ApObservation apObservation;
std::atomic<uint32_t> scanDoneEvents{0};
std::atomic<uint32_t> scanDoneStatus{0};
uint32_t staBeginCalls = 0;
uint32_t staDisconnectCalls = 0;
uint32_t scanRejected = 0;
int scanStopResult = ESP_OK;
uint32_t channelChanges = 0;
uint32_t channelChangedMs = 0;
unsigned lastObservedChannel = 0;

balance::ApObservation apEvents() {
    portENTER_CRITICAL(&apObservationMux);
    const auto copy = apObservation;
    portEXIT_CRITICAL(&apObservationMux);
    return copy;
}
const char* modeName(unsigned mode) {
    switch (mode) { case WIFI_MODE_NULL: return "OFF"; case WIFI_MODE_STA: return "STA";
    case WIFI_MODE_AP: return "AP"; case WIFI_MODE_APSTA: return "AP_STA"; }
    return "UNKNOWN";
}
struct DriverView {
    balance::ApDriverEvidence evidence;
    balance::ApObservation event;
    esp_err_t modeResult = ESP_FAIL, configResult = ESP_FAIL, channelResult = ESP_FAIL;
    esp_err_t clientsResult = ESP_FAIL, macResult = ESP_FAIL, psResult = ESP_FAIL;
    esp_err_t staConfigResult = ESP_FAIL;
    wifi_mode_t mode = WIFI_MODE_NULL;
    uint8_t channel = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    unsigned clients = 0, configChannel = 0, beaconMs = 0, hidden = 0, auth = 0;
    wifi_ps_type_t powerSave = WIFI_PS_NONE;
    char ssid[33]{};
    char staConfiguredSsid[33]{};
    char bssid[18]{};
    bool ready = false;
};
DriverView readDriver() {
    DriverView d;
    d.event = apEvents();
    d.modeResult = esp_wifi_get_mode(&d.mode);
    wifi_config_t config{};
    d.configResult = esp_wifi_get_config(WIFI_IF_AP, &config);
    if (d.configResult == ESP_OK) {
        const size_t length = config.ap.ssid_len > 0 ?
            (config.ap.ssid_len <= 32 ? config.ap.ssid_len : 32) : strnlen(reinterpret_cast<const char*>(config.ap.ssid), 32);
        memcpy(d.ssid, config.ap.ssid, length);
        d.configChannel = config.ap.channel; d.beaconMs = config.ap.beacon_interval;
        d.hidden = config.ap.ssid_hidden; d.auth = config.ap.authmode;
    }
    // wifi_config_t contains the AP password. Never print/dump the struct or retain it.
    volatile uint8_t* clear = reinterpret_cast<volatile uint8_t*>(&config);
    for (size_t i = 0; i < sizeof(config); ++i) clear[i] = 0;
    d.staConfigResult = esp_wifi_get_config(WIFI_IF_STA, &config);
    if (d.staConfigResult == ESP_OK) memcpy(d.staConfiguredSsid, config.sta.ssid, 32);
    for (size_t i = 0; i < sizeof(config); ++i) clear[i] = 0;
    d.channelResult = esp_wifi_get_channel(&d.channel, &d.secondary);
    wifi_sta_list_t clients{};
    d.clientsResult = esp_wifi_ap_get_sta_list(&clients);
    if (d.clientsResult == ESP_OK) d.clients = clients.num;
    uint8_t mac[6]{};
    d.macResult = esp_wifi_get_mac(WIFI_IF_AP, mac);
    if (d.macResult == ESP_OK) {
        const int size = snprintf(d.bssid, sizeof(d.bssid), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        if (size != 17) { d.bssid[0] = 0; d.macResult = ESP_FAIL; }
    }
    d.psResult = esp_wifi_get_ps(&d.powerSave);
    auto* interface = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ip{};
    d.evidence.modeOk = d.modeResult == ESP_OK; d.evidence.mode = d.mode;
    d.evidence.configOk = d.configResult == ESP_OK;
    d.evidence.channelOk = d.channelResult == ESP_OK; d.evidence.channel = d.channel;
    d.evidence.clientsOk = d.clientsResult == ESP_OK;
    d.evidence.interfaceUp = (WiFi.getStatusBits() & AP_STARTED_BIT) != 0 && interface && esp_netif_is_netif_up(interface);
    d.evidence.ipAssigned = interface && esp_netif_get_ip_info(interface, &ip) == ESP_OK && ip.ip.addr != 0;
    d.evidence.ssidHidden = d.hidden != 0; d.evidence.ssidNonempty = d.ssid[0] != 0;
    d.evidence.ssidMatchesExpected = strcmp(d.ssid, BALANCEBOT_AP_SSID) == 0;
    d.ready = balance::apDriverReady(d.event, d.evidence);
    return d;
}
String scanJson = "{\"state\":\"idle\",\"networks\":[]}";
bool mdnsReady = false;
String staIp, staGateway, staMask;
char apBssid[18]{};

class NvsWifiSlots final : public balance::WifiSlotStorage {
public:
    bool readSlot(uint8_t slot, balance::WifiRecord& record) override {
        nvs_handle_t handle;
        if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
        size_t size = sizeof(record.bytes);
        const auto result = nvs_get_blob(handle, slot == 0 ? "slot0" : "slot1", record.bytes, &size);
        nvs_close(handle);
        return result == ESP_OK && size == sizeof(record.bytes);
    }
    bool writeSlot(uint8_t slot, const balance::WifiRecord& record) override {
        nvs_handle_t handle;
        if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
        const auto result = nvs_set_blob(handle, slot == 0 ? "slot0" : "slot1", record.bytes, sizeof(record.bytes));
        const bool ok = result == ESP_OK && nvs_commit(handle) == ESP_OK;
        nvs_close(handle);
        return ok;
    }
    bool readActive(uint8_t& slot) override {
        nvs_handle_t handle;
        slot = 255;
        const auto opened = nvs_open(kNamespace, NVS_READONLY, &handle);
        if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
        if (opened != ESP_OK) return false;
        const auto result = nvs_get_u8(handle, "active", &slot);
        nvs_close(handle);
        return result == ESP_OK || result == ESP_ERR_NVS_NOT_FOUND;
    }
    bool writeActive(uint8_t slot) override {
        nvs_handle_t handle;
        if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
        uint8_t previous = 255;
        const auto previousResult = nvs_get_u8(handle, "active", &previous);
        if (previousResult != ESP_OK && previousResult != ESP_ERR_NVS_NOT_FOUND) {
            nvs_close(handle); return false;
        }
        bool ok = nvs_set_u8(handle, "active", slot) == ESP_OK && nvs_commit(handle) == ESP_OK;
        uint8_t verified = 255;
        ok = ok && nvs_get_u8(handle, "active", &verified) == ESP_OK && verified == slot;
        if (!ok) {
            // Only touches our selector. Both credential slots and control NVS are retained.
            const bool restored = nvs_set_u8(handle, "active", previous) == ESP_OK && nvs_commit(handle) == ESP_OK;
            consoleLog(balance::LogKind::Critical, "[NET] NVS selector failure rollback=%u; AP preservado\n", restored);
        }
        nvs_close(handle);
        return ok;
    }
} slots;

class EspWifiRadio final : public balance::WifiRadio {
    balance::WifiCredentials pending_;
    uint32_t epoch_ = 0, startAt_ = 0;
public:
    void disconnect() override {
        if (invalidate) invalidate();
        epoch_ = 0; balance::clearWifiCredentials(pending_);
        portENTER_CRITICAL(&leaseMux);
        activeEpoch.store(0); associatedEpoch.store(0); leaseReady.store(false);
        failedEpoch = 0; leasePending = false; expectedSsid[0] = 0;
        portEXIT_CRITICAL(&leaseMux);
        ++staDisconnectCalls;
        WiFi.disconnect(false, false); // Neither disable AP nor erase SDK NVS.
    }
    void connect(const balance::WifiCredentials& credentials, uint32_t epoch) override {
        pending_ = credentials; epoch_ = epoch; startAt_ = millis();
    }
    void service() {
        if (!epoch_ || millis() - startAt_ < 250) return;
        wifi_ap_record_t previous{};
        if (esp_wifi_sta_get_ap_info(&previous) == ESP_OK) return; // disconnect barrier
        auto* interface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!interface) return;
        // Discard the previous lease before arming a new association/DHCP generation.
        esp_netif_dhcpc_stop(interface);
        esp_netif_ip_info_t zero{};
        if (esp_netif_set_ip_info(interface, &zero) != ESP_OK || esp_netif_dhcpc_start(interface) != ESP_OK) {
            reportFailure(epoch_, balance::WifiError::Dhcp);
            epoch_ = 0; balance::clearWifiCredentials(pending_); return;
        }
        portENTER_CRITICAL(&leaseMux);
        memcpy(expectedSsid, pending_.ssid, sizeof(expectedSsid));
        activeEpoch.store(epoch_);
        portEXIT_CRITICAL(&leaseMux);
        WiFi.setMinSecurity(pending_.password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN);
        epoch_ = 0;
        ++staBeginCalls;
        WiFi.begin(pending_.ssid, pending_.password);
        balance::clearWifiCredentials(pending_);
        consoleLog(balance::LogKind::Debug, "[NET] STA tentativa iniciada (credenciais omitidas)\n");
    }
} radio;
balance::WifiCredentialJournal journal(slots);
balance::WifiManager manager(radio, journal);

void randomHex(char* output, size_t bytes) {
    const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < bytes; ++i) {
        const uint8_t value = static_cast<uint8_t>(esp_random());
        output[i * 2] = digits[value >> 4]; output[i * 2 + 1] = digits[value & 15];
    }
    output[bytes * 2] = 0;
}
String quote(const char* text) {
    // Strings here are bounded SSIDs, IPv4s, nonces or fixed enum messages.
    String output = "\"";
    const char hex[] = "0123456789abcdef";
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        if (*p == '"' || *p == '\\') { output += '\\'; output += char(*p); }
        else if (*p < 32) { output += "\\u00"; output += hex[*p >> 4]; output += hex[*p & 15]; }
        else output += char(*p);
    }
    output += '"'; return output;
}
void reply(int status, const String& body) {
    http->sendHeader("Cache-Control", "no-store");
    http->sendHeader("X-Content-Type-Options", "nosniff");
    http->send(status, "application/json", body);
}
bool validPost() {
    const String contentType = http->header("Content-Type");
    if (!contentType.startsWith("application/x-www-form-urlencoded") ||
        !http->hasHeader("Content-Length") || http->header("Content-Length").toInt() > 512 || http->args() > 4) {
        reply(400, "{\"error\":\"invalid_form\"}"); return false;
    }
    // Reject duplicate/unknown fields. Access is intentionally open on trusted AP/LAN.
    for (int i = 0; i < http->args(); ++i) {
        const String name = http->argName(i);
        if (name != "ssid" && name != "password" && name != "open" && name != "trial_id") {
            reply(400, "{\"error\":\"unknown_field\"}"); return false;
        }
        for (int j = 0; j < i; ++j) if (http->argName(j) == name) {
            reply(400, "{\"error\":\"duplicate_field\"}"); return false;
        }
    }
    return true;
}
bool linkReady() {
    if (!leaseReady.load() || WiFi.status() != WL_CONNECTED || uint32_t(WiFi.localIP()) == 0) return false;
    wifi_ap_record_t ap{};
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
           memcmp(ap.ssid, expectedSsid, strlen(expectedSsid)) == 0 &&
           (strlen(expectedSsid) == 32 || ap.ssid[strlen(expectedSsid)] == 0);
}
bool overlaps() {
    return linkReady() && balance::subnetOverlapsAp(ntohl(uint32_t(WiFi.localIP())), ntohl(uint32_t(WiFi.subnetMask())));
}
String stateJson() {
    const bool connected = linkReady() && !overlaps();
    const uint32_t now = millis();
    String result = "{\"state\":" + quote(balance::wifiPhaseName(manager.phase()));
    result += ",\"error\":" + quote(balance::wifiErrorName(manager.error()));
    result += ",\"result\":" + quote(manager.result());
    result += ",\"result_error\":" + quote(balance::wifiErrorName(manager.resultError()));
    result += ",\"result_ip\":" + quote(IPAddress(htonl(manager.resultIp())).toString().c_str());
    result += ",\"epoch\":" + String(manager.epoch());
    result += ",\"has_saved\":" + String(manager.hasSaved() ? "true" : "false");
    result += ",\"trial_id\":" + quote(manager.trialActive() ? trialId : "");
    result += ",\"retry_in_ms\":" + String(manager.retryInMs(now));
    result += ",\"sta_connected\":" + String(connected ? "true" : "false");
    result += ",\"sta_ip\":" + quote(connected ? WiFi.localIP().toString().c_str() : "0.0.0.0");
    result += ",\"sta_gateway\":" + quote(connected ? WiFi.gatewayIP().toString().c_str() : "0.0.0.0");
    result += ",\"sta_rssi\":" + String(connected ? WiFi.RSSI() : 0);
    result += ",\"sta_channel\":" + String(connected ? WiFi.channel() : 0);
    uint8_t channel = 0; wifi_second_chan_t secondary;
    esp_wifi_get_channel(&channel, &secondary);
    result += ",\"ap_channel\":" + String(channel);
    result += ",\"ap_clients\":" + String(WiFi.softAPgetStationNum());
    result += ",\"ap_ssid\":" + quote(WiFi.softAPSSID().c_str());
    result += ",\"ap_ip\":" + quote(WiFi.softAPIP().toString().c_str());
    esp_netif_ip_info_t apInfo{};
    auto* apInterface = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (apInterface) esp_netif_get_ip_info(apInterface, &apInfo);
    const DriverView ap = readDriver();
    const bool apReady = ap.ready;
    result += ",\"ap_ready\":" + String(apReady ? "true" : "false");
    result += ",\"ap_last_event\":" + quote(balance::apEventName(ap.event.last));
    result += ",\"ap_stops\":" + String(ap.event.stops);
    result += ",\"wifi_driver_mode\":" + quote(ap.modeResult == ESP_OK ? modeName(ap.mode) : "UNKNOWN");
    result += ",\"scan_starts\":" + String(scanSession.starts());
    result += ",\"ap_gateway\":" + quote(IPAddress(apInfo.gw.addr).toString().c_str());
    result += ",\"ap_netmask\":" + quote(IPAddress(apInfo.netmask.addr).toString().c_str());
    result += ",\"disconnect_reason\":" + String(disconnectReason.load());
    result += ",\"mdns_ready\":" + String(mdnsReady ? "true" : "false") + "}";
    return result;
}
void wifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_AP_START || event == ARDUINO_EVENT_WIFI_AP_STOP) {
        const auto transition = event == ARDUINO_EVENT_WIFI_AP_START ? balance::ApEvent::Start : balance::ApEvent::Stop;
        const uint32_t now = millis();
        portENTER_CRITICAL(&apObservationMux);
        balance::observeAp(apObservation, transition, now);
        portEXIT_CRITICAL(&apObservationMux);
        if (transition == balance::ApEvent::Stop && invalidate) invalidate();
        consoleLog(transition == balance::ApEvent::Stop ? balance::LogKind::Critical : balance::LogKind::Transition,
                   "[Wi-Fi AP] %s em %lu ms (evento real; sem restart automatico).\n",
                   balance::apEventName(transition), static_cast<unsigned long>(now));
    }
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED || event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
        portENTER_CRITICAL(&apObservationMux);
        if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) ++apObservation.joins; else ++apObservation.leaves;
        portEXIT_CRITICAL(&apObservationMux);
        consoleLog(balance::LogKind::Transition, "[Wi-Fi AP] cliente %s em %lu ms.\n",
                   event == ARDUINO_EVENT_WIFI_AP_STACONNECTED ? "associado" : "desassociado", static_cast<unsigned long>(millis()));
    }
    if (event == ARDUINO_EVENT_WIFI_SCAN_DONE) {
        scanDoneEvents.fetch_add(1); scanDoneStatus.store(info.wifi_scan_done.status);
    }

    uint32_t bit = 0;
    if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
        bit = 1;
        const uint32_t epoch = activeEpoch.load();
        wifi_ap_record_t current{};
        portENTER_CRITICAL(&leaseMux);
        char expected[33]; memcpy(expected, expectedSsid, sizeof(expected));
        portEXIT_CRITICAL(&leaseMux);
        if (epoch && strlen(expected) == info.wifi_sta_connected.ssid_len &&
            memcmp(expected, info.wifi_sta_connected.ssid, strlen(expected)) == 0 &&
            esp_wifi_sta_get_ap_info(&current) == ESP_OK &&
            memcmp(current.bssid, info.wifi_sta_connected.bssid, 6) == 0) {
            portENTER_CRITICAL(&leaseMux);
            if (activeEpoch.load() == epoch) {
                memcpy(associatedBssid, current.bssid, 6);
                associatedEpoch.store(epoch);
            }
            portEXIT_CRITICAL(&leaseMux);
        }
    }
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        bit = 2;
        const uint32_t epoch = activeEpoch.load();
        wifi_ap_record_t live{};
        // A queued disconnect cannot invalidate a newer association already live in the driver.
        if (epoch && esp_wifi_sta_get_ap_info(&live) != ESP_OK) {
            leaseReady.store(false); associatedEpoch.store(0);
            disconnectReason.store(info.wifi_sta_disconnected.reason);
            const unsigned reason = info.wifi_sta_disconnected.reason;
            portENTER_CRITICAL(&leaseMux);
            const bool targetMatches = strlen(expectedSsid) == info.wifi_sta_disconnected.ssid_len &&
                memcmp(expectedSsid, info.wifi_sta_disconnected.ssid, strlen(expectedSsid)) == 0;
            portEXIT_CRITICAL(&leaseMux);
            if (targetMatches && (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                reason == WIFI_REASON_HANDSHAKE_TIMEOUT)) {
                reportFailure(epoch, balance::WifiError::Authentication);
            }
        }
    }
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        bit = 4;
        const uint32_t epoch = activeEpoch.load();
        wifi_ap_record_t current{};
        esp_netif_ip_info_t live{};
        auto* interface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_dhcp_status_t dhcp = ESP_NETIF_DHCP_INIT;
        const bool driverMatches = epoch && associatedEpoch.load() == epoch &&
            esp_wifi_sta_get_ap_info(&current) == ESP_OK && interface && info.got_ip.esp_netif == interface &&
            esp_netif_get_ip_info(interface, &live) == ESP_OK &&
            esp_netif_dhcpc_get_status(interface, &dhcp) == ESP_OK && dhcp == ESP_NETIF_DHCP_STARTED &&
            live.ip.addr == info.got_ip.ip_info.ip.addr && live.gw.addr == info.got_ip.ip_info.gw.addr &&
            live.netmask.addr == info.got_ip.ip_info.netmask.addr;
        if (driverMatches) {
            portENTER_CRITICAL(&leaseMux);
            if (activeEpoch.load() == epoch && associatedEpoch.load() == epoch &&
                memcmp(current.bssid, associatedBssid, 6) == 0 &&
                memcmp(current.ssid, expectedSsid, strlen(expectedSsid)) == 0 &&
                (strlen(expectedSsid) == 32 || current.ssid[strlen(expectedSsid)] == 0)) {
                pendingLease = {epoch, ntohl(live.ip.addr), ntohl(live.netmask.addr), ntohl(live.gw.addr)};
                leasePending = true;
                leaseReady.store(balance::validateWifiLease(pendingLease) == balance::WifiError::None);
            }
            portEXIT_CRITICAL(&leaseMux);
        }
    }
    if (event == ARDUINO_EVENT_WIFI_STA_LOST_IP) { bit = 8; if (uint32_t(WiFi.localIP()) == 0) leaseReady.store(false); }
    if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) bit = 16;
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) bit = 32;
    if (bit) {
        if (bit != 32 && invalidate) invalidate();
        events.fetch_or(bit);
    }
}
void finishScan() {
    if (!scanSession.running()) return;
    const int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING && millis() - scanSession.startMs() < 10000) return;
    scanSession.finished(millis(), count);
    if (count < 0) {
        scanStopResult = esp_wifi_scan_stop(); WiFi.scanDelete();
        consoleLog(balance::LogKind::Transition, "[Wi-Fi scan] falha/timeout; stop=%s; sem restart do radio.\n", esp_err_to_name(scanStopResult));
        scanJson = "{\"state\":\"failed\",\"error\":\"scan_failed_or_timeout\",\"networks\":[]}";
        return;
    }
    scanJson = "{\"state\":\"complete\",\"networks\":[";
    unsigned included = 0;
    for (int i = 0; i < count && included < 16; ++i) {
        const int channel = WiFi.channel(i);
        if (!balance::is24GhzChannel(channel)) continue;
        if (included++) scanJson += ',';
        scanJson += "{\"ssid\":" + quote(WiFi.SSID(i).c_str());
        scanJson += ",\"channel\":" + String(channel) + ",\"rssi\":" + String(WiFi.RSSI(i));
        scanJson += ",\"auth\":" + String(static_cast<unsigned>(WiFi.encryptionType(i))) + "}";
    }
    scanJson += "],\"total_found\":" + String(count) + ",\"shown\":" + String(included) + "}";
    consoleLog(balance::LogKind::Transition, "[Wi-Fi scan] concluido; total=%d; duracao=%lu ms.\n", count,
               static_cast<unsigned long>(scanSession.endMs() - scanSession.startMs()));
    WiFi.scanDelete();
}
}

void balanceNetworkPrepare(void (*invalidateMotion)()) {
    invalidate = invalidateMotion;
    WiFi.persistent(false); // SDK STA/AP config in RAM; only our confirmed journal persists STA.
    WiFi.setAutoReconnect(false);
    WiFi.onEvent(wifiEvent);
}
void balanceNetworkSetup(WebServer& server) {
    http = &server;
    const char* headers[] = {"Content-Type", "Content-Length"};
    http->collectHeaders(headers, 2);
    http->on("/api/network/status", HTTP_GET, [] { reply(200, stateJson()); });
    http->on("/api/network/scan", HTTP_GET, [] { reply(200, scanJson); });
    http->on("/api/network/scan", HTTP_POST, [] {
        if (!validPost()) return;
        const auto phase = manager.phase();
        const bool busy = manager.trialActive() || phase == balance::WifiPhase::Connecting || phase == balance::WifiPhase::Backoff;
        if (!scanSession.allowed(millis(), busy, readDriver().ready)) {
            ++scanRejected;
            reply(409, "{\"error\":\"network_busy\"}"); return;
        }
        WiFi.scanDelete();
        if (invalidate) invalidate();
        const int result = WiFi.scanNetworks(true, true, false, 120);
        scanSession.started(millis(), result);
        consoleLog(balance::LogKind::Transition, "[Wi-Fi scan] inicio explicito HTTP; resultado=%d; tentativa=%lu.\n", result,
                   static_cast<unsigned long>(scanSession.starts()));
        if (result == WIFI_SCAN_FAILED) { reply(503, "{\"error\":\"scan_failed\"}"); return; }
        scanJson = "{\"state\":\"running\",\"networks\":[]}";
        reply(202, scanJson);
    });
    http->on("/api/network/connect", HTTP_POST, [] {
        if (!validPost()) return;
        if (scanSession.running() || manager.trialActive()) { reply(409, "{\"error\":\"network_busy\"}"); return; }
        String ssid = http->arg("ssid");
        String password = http->arg("password");
        if (ssid.length() < 1 || ssid.length() > 32 || password.length() > 64 ||
            ssid.length() != strlen(ssid.c_str()) || password.length() != strlen(password.c_str()) ||
            (password.isEmpty() && http->arg("open") != "1")) {
            reply(400, "{\"error\":\"invalid_credentials\"}"); return;
        }
        balance::WifiCredentials candidate;
        memcpy(candidate.ssid, ssid.c_str(), ssid.length());
        memcpy(candidate.password, password.c_str(), password.length());
        for (size_t i = 0; i < password.length(); ++i) password.setCharAt(i, 0);
        if (!balance::validWifiCredentials(candidate)) {
            balance::clearWifiCredentials(candidate);
            reply(400, "{\"error\":\"invalid_credentials\"}"); return;
        }
        randomHex(trialId, 16);
        const bool accepted = manager.connectAndSave(candidate, millis());
        balance::clearWifiCredentials(candidate);
        reply(accepted ? 202 : 409, accepted ? stateJson() : "{\"error\":\"network_busy\"}");
    });
    http->on("/api/network/cancel", HTTP_POST, [] {
        if (!validPost()) return;
        if (!manager.trialActive() || http->arg("trial_id") != trialId) {
            reply(409, "{\"error\":\"trial_mismatch\"}"); return;
        }
        manager.cancel(millis());
        reply(200, stateJson());
    });
    manager.begin(millis());
    consoleLog(balance::LogKind::Debug, "[NET] saved=%u state=%s error=%s\n", manager.hasSaved(),
                  balance::wifiPhaseName(manager.phase()), balance::wifiErrorName(manager.error()));
#if BALANCEBOT_ENABLE_MDNS
    mdnsReady = MDNS.begin("BalanceBot");
    if (mdnsReady) { MDNS.addService("http", "tcp", 80); MDNS.addService("ws", "tcp", 81); }
#endif
}
void balanceNetworkLoop() {
    // Read-only observation once per second; never restores/restarts Wi-Fi.
    static uint32_t lastDriverPoll = 0;
    if (millis() - lastDriverPoll >= 1000) {
        lastDriverPoll = millis();
        const DriverView view = readDriver();
        if (view.channelResult == ESP_OK && view.channel != lastObservedChannel) {
            if (lastObservedChannel != 0) {
                ++channelChanges; channelChangedMs = millis();
                consoleLog(balance::LogKind::Transition, "[Wi-Fi AP] canal observado %u -> %u em %lu ms; scan=%s STA=%s.\n",
                           lastObservedChannel, view.channel, static_cast<unsigned long>(channelChangedMs),
                           balance::scanStateName(scanSession.state()), balance::wifiPhaseName(manager.phase()));
            }
            lastObservedChannel = view.channel;
        }
        static bool checked = false, previousReady = false;
        if (checked && view.ready != previousReady) consoleLog(view.ready ? balance::LogKind::Transition : balance::LogKind::Critical,
            "[Wi-Fi AP] driver consistente=%u; evento=%s modo_rc=%d config_rc=%d; nao comprova beacons RF.\n",
            view.ready, balance::apEventName(view.event.last), view.modeResult, view.configResult);
        previousReady = view.ready; checked = true;
    }
    finishScan();
    const uint32_t observed = events.exchange(0);
    if (observed) {
        if (observed & 1) consoleLog(balance::LogKind::Debug, "[NET] STA associada\n");
        if (observed & 2) consoleLog(balance::LogKind::Debug, "[NET] STA desconectada\n");
        if (observed & 4) consoleLog(balance::LogKind::Debug, "[NET] STA recebeu IP\n");
        if (observed & 8) consoleLog(balance::LogKind::Debug, "[NET] STA perdeu IP\n");
        if (observed & 16) consoleLog(balance::LogKind::Debug, "[NET] cliente saiu do AP\n");
        if (observed & 32) consoleLog(balance::LogKind::Debug, "[NET] cliente entrou no AP\n");
        consoleLog(balance::LogKind::Debug, "[NET] events=%lu disconnect_reason=%u STA_IP=%s channel=%ld RSSI=%ld AP_clients=%u\n",
                      static_cast<unsigned long>(observed), disconnectReason.load(),
                      WiFi.localIP().toString().c_str(), static_cast<long>(WiFi.channel()),
                      static_cast<long>(linkReady() ? WiFi.RSSI() : 0), WiFi.softAPgetStationNum());
    }
    radio.service(); // bounded, nonblocking; never restarts AP or the servers
    const auto previous = manager.phase();
    balance::WifiLease lease;
    portENTER_CRITICAL(&leaseMux);
    const bool received = leasePending;
    if (received) { lease = pendingLease; leasePending = false; }
    portEXIT_CRITICAL(&leaseMux);
    const uint32_t epoch = associatedEpoch.load();
    if (epoch) manager.associated(epoch);
    esp_netif_ip_info_t live{};
    auto* staInterface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    const bool leaseStillCurrent = received && WiFi.status() == WL_CONNECTED && staInterface &&
        esp_netif_get_ip_info(staInterface, &live) == ESP_OK &&
        ntohl(live.ip.addr) == lease.ip && ntohl(live.netmask.addr) == lease.mask && ntohl(live.gw.addr) == lease.gateway;
    if (leaseStillCurrent && lease.epoch == activeEpoch.load() && epoch == lease.epoch) {
        manager.gotIp(millis(), lease);
        if (balance::validateWifiLease(lease) != balance::WifiError::None) {
            auto* interface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (interface) {
                const auto stopped = esp_netif_dhcpc_stop(interface);
                esp_netif_ip_info_t zero{};
                const auto cleared = esp_netif_set_ip_info(interface, &zero);
                if (cleared != ESP_OK) consoleLog(balance::LogKind::Critical,
                    "[Wi-Fi] Falha ao limpar lease STA rejeitado: stop=%d clear=%d; AP preservado.\n", stopped, cleared);
            }
        }
    }
    portENTER_CRITICAL(&leaseMux);
    const uint32_t failure = failedEpoch; const auto failureError = pendingFailure; failedEpoch = 0;
    portEXIT_CRITICAL(&leaseMux);
    if (failure) manager.failed(millis(), failure, failureError);
    if (scanSession.running() && previous == balance::WifiPhase::Backoff) return;
    manager.tick(millis(), linkReady(), false);
    if (previous == balance::WifiPhase::Provisioning && manager.phase() == balance::WifiPhase::Online)
        consoleLog(balance::LogKind::Transition, "[Wi-Fi] Configuracao salva automaticamente e verificada na NVS.\n");
    if (previous != manager.phase()) consoleLog(balance::LogKind::Debug, "[NET] state=%s error=%s\n",
        balance::wifiPhaseName(manager.phase()), balance::wifiErrorName(manager.error()));
    static bool wasOnline = false;
    static bool offlineNotified = false;
    static balance::WifiError lastImportantError = balance::WifiError::None;
    const bool online = linkReady();
    if (online && !wasOnline) {
        consoleLog(balance::LogKind::Transition, "[Wi-Fi] STA conectada: IP=%s canal=%ld RSSI=%ld dBm\n",
                   WiFi.localIP().toString().c_str(), static_cast<long>(WiFi.channel()), static_cast<long>(WiFi.RSSI()));
        offlineNotified = false;
    } else if (!online && !offlineNotified && (wasOnline || manager.phase() == balance::WifiPhase::Backoff)) {
        consoleLog(balance::LogKind::Transition, "[Wi-Fi] STA desconectada; AP preservado. Retentativas em segundo plano.\n");
        offlineNotified = true;
    }
    if (manager.error() != lastImportantError &&
        (manager.error() == balance::WifiError::Conflict || manager.error() == balance::WifiError::Storage)) {
        consoleLog(balance::LogKind::Critical, "[Wi-Fi] Falha importante: %s; AP preservado.\n", balance::wifiErrorName(manager.error()));
    }
    lastImportantError = manager.error(); wasOnline = online;
}
void balanceNetworkPrintDriverStatus() {
    const DriverView d = readDriver();
    Serial.println("--- Wi-Fi driver (consultas somente leitura; beacons RF NAO verificados) ---");
    Serial.printf("esp_wifi_get_mode: rc=%d (%s), modo=%u/%s | Arduino modo=%u AP_STARTED_BIT=%u\n",
                  d.modeResult, esp_err_to_name(d.modeResult), static_cast<unsigned>(d.mode), d.modeResult == ESP_OK ? modeName(d.mode) : "UNKNOWN",
                  static_cast<unsigned>(WiFi.getMode()), d.evidence.interfaceUp);
    Serial.printf("esp_wifi_get_config(AP): rc=%d (%s), SSID=%s oculto=%u canal_cfg=%u beacon=%u ms auth=%u\n",
                  d.configResult, esp_err_to_name(d.configResult), d.ssid, d.hidden, d.configChannel, d.beaconMs, d.auth);
    Serial.printf("esp_wifi_get_config(STA): rc=%d (%s), configurado=%u | app_STA=%s\n",
                  d.staConfigResult, esp_err_to_name(d.staConfigResult),
                  d.staConfiguredSsid[0] != 0, balance::wifiPhaseName(manager.phase()));
    Serial.printf("SSID AP esperado=%s coincide=%u | IP atribuido=%u (sem prova RF)\n",
                  BALANCEBOT_AP_SSID, d.evidence.ssidMatchesExpected, d.evidence.ipAssigned);
    Serial.printf("esp_wifi_get_channel: rc=%d (%s), primario=%u secundario=%d | BSSID_AP=%s MAC_rc=%d\n",
                  d.channelResult, esp_err_to_name(d.channelResult), d.channel, d.secondary, d.bssid, d.macResult);
    Serial.printf("esp_wifi_ap_get_sta_list: rc=%d (%s), clientes=%u | PS_rc=%d PS=%d (NONE=0)\n",
                  d.clientsResult, esp_err_to_name(d.clientsResult), d.clients, d.psResult, d.powerSave);
    Serial.printf("AP evento=%s em %lu ms START=%lu STOP=%lu JOIN=%lu LEAVE=%lu | consistente=%u\n",
                  balance::apEventName(d.event.last), static_cast<unsigned long>(d.event.atMs),
                  static_cast<unsigned long>(d.event.starts), static_cast<unsigned long>(d.event.stops),
                  static_cast<unsigned long>(d.event.joins), static_cast<unsigned long>(d.event.leaves), d.ready);
    Serial.printf("Canal alterou=%lu ultima_alteracao=%lu ms | STA begin_calls=%lu disconnect_calls=%lu\n",
                  static_cast<unsigned long>(channelChanges), static_cast<unsigned long>(channelChangedMs),
                  static_cast<unsigned long>(staBeginCalls), static_cast<unsigned long>(staDisconnectCalls));
    Serial.printf("Ultimo scan=%s inicio=%lu fim=%lu ms result=%d starts=%lu recusados=%lu SCAN_DONE=%lu status=%lu stop_rc=%d\n",
                  balance::scanStateName(scanSession.state()), static_cast<unsigned long>(scanSession.startMs()),
                  static_cast<unsigned long>(scanSession.endMs()), scanSession.result(), static_cast<unsigned long>(scanSession.starts()),
                  static_cast<unsigned long>(scanRejected), static_cast<unsigned long>(scanDoneEvents.load()),
                  static_cast<unsigned long>(scanDoneStatus.load()), scanStopResult);
}

void balanceNetworkHealth(balance::HealthDiagnostics& health) {
    const bool connected = linkReady() && !overlaps();
    staIp = connected ? WiFi.localIP().toString() : "0.0.0.0";
    staGateway = connected ? WiFi.gatewayIP().toString() : "0.0.0.0";
    staMask = connected ? WiFi.subnetMask().toString() : "0.0.0.0";
    health.staConnected = connected;
    health.staSsid = ""; health.staIp = staIp.c_str();
    health.staGateway = staGateway.c_str(); health.staNetmask = staMask.c_str();
    health.staState = balance::wifiPhaseName(manager.phase());
    health.staError = balance::wifiErrorName(manager.error());
    health.staRssi = connected ? WiFi.RSSI() : 0;
    health.staChannel = connected ? WiFi.channel() : 0;
    uint8_t channel = 0; wifi_second_chan_t secondary;
    esp_wifi_get_channel(&channel, &secondary);
    health.apChannel = channel;
    health.staDisconnectReason = disconnectReason.load();
    health.mdnsReady = mdnsReady;
    const DriverView ap = readDriver();
    memcpy(apBssid, ap.bssid, sizeof(apBssid));
    health.apReady = ap.ready;
    health.wifiDriverMode = ap.modeResult == ESP_OK ? modeName(ap.mode) : "UNKNOWN";
    health.apModeResult = ap.modeResult; health.apConfigResult = ap.configResult;
    health.apLastEvent = balance::apEventName(ap.event.last);
    health.apEventMs = ap.event.atMs; health.apStarts = ap.event.starts; health.apStops = ap.event.stops;
    health.apBssid = apBssid;
    health.apChannel = ap.channel; health.apClients = ap.clients;
    health.scanState = balance::scanStateName(scanSession.state()); health.scanStarts = scanSession.starts();
}
#endif
