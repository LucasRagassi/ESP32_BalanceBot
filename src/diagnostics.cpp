#include "balance/diagnostics.hpp"

#include <cstdarg>
#include <cstdio>

namespace balance {
namespace {

class JsonWriter {
public:
    JsonWriter(char* output, size_t capacity) : output_(output), capacity_(capacity) {}

    void append(const char* format, ...) {
        if (!valid_) return;
        va_list arguments;
        va_start(arguments, format);
        const int count = vsnprintf(output_ + used_, capacity_ - used_, format, arguments);
        va_end(arguments);
        valid_ = formatFits(count, capacity_ - used_);
        if (valid_) used_ += static_cast<size_t>(count);
    }

    void string(const char* key, const char* value) {
        append("\"%s\":\"", key);
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value);
             valid_ && *p != 0; ++p) {
            if (*p == '"' || *p == '\\') append("\\%c", *p);
            else if (*p < 0x20) append("\\u%04x", static_cast<unsigned>(*p));
            else append("%c", *p);
        }
        append("\",");
    }

    void boolean(const char* key, bool value) {
        append("\"%s\":%s,", key, value ? "true" : "false");
    }
    void number(const char* key, uint32_t value) {
        append("\"%s\":%lu,", key, static_cast<unsigned long>(value));
    }
    bool valid() const { return valid_; }

private:
    char* output_;
    size_t capacity_;
    size_t used_ = 0;
    bool valid_ = true;
};

}

bool heartbeatFresh(bool seen, uint32_t nowMs, uint32_t publishedAtMs) {
    return seen && static_cast<uint32_t>(nowMs - publishedAtMs) <= kControlHeartbeatMaxAgeMs;
}

bool healthOk(const HealthDiagnostics& d) {
    return d.apConfigReady && d.apReady && d.httpListening && d.websocketListening &&
           d.littleFsReady && d.indexReady && d.scriptReady && d.styleReady &&
           d.controlTaskCreated && heartbeatFresh(d.heartbeatSeen, d.uptimeMs, d.heartbeatAtMs) &&
           d.commandQueueReady && d.configQueueReady;
}

bool renderHealthJson(const HealthDiagnostics& d, char* output, size_t capacity) {
    if (output == nullptr || capacity == 0) return false;
    JsonWriter json(output, capacity);
    json.append("{");
    json.boolean("ok", healthOk(d));
    json.string("version", d.version);
    json.string("commit", d.commit);
    json.string("tree_state", d.treeState);
    json.string("source_sha256", d.sourceSha256);
    json.string("build_utc", d.buildUtc);
    json.string("mode", d.mode);
    json.number("uptime_ms", d.uptimeMs);
    json.number("free_heap", d.freeHeap);
    json.number("min_free_heap", d.minFreeHeap);
    json.string("reset_reason", d.resetReason);
    json.string("ap_ssid", d.ssid);
    json.string("ap_ip", d.ip);
    json.string("ap_gateway", d.gateway);
    json.string("ap_netmask", d.netmask);
    json.number("ap_clients", d.apClients);
    json.string("wifi_mode", d.wifiDriverMode);
    json.string("ap_last_event", d.apLastEvent);
    json.string("ap_bssid", d.apBssid);
    json.number("ap_event_ms", d.apEventMs);
    json.number("ap_start_events", d.apStarts);
    json.number("ap_stop_events", d.apStops);
    json.append("\"ap_get_mode_rc\":%d,\"ap_get_config_rc\":%d,", d.apModeResult, d.apConfigResult);
    json.string("scan_state", d.scanState);
    json.number("scan_starts", d.scanStarts);
    json.number("ap_channel", d.apChannel);
    json.boolean("sta_connected", d.staConnected);
    json.string("sta_ssid", ""); // Private STA identity is never returned by public diagnostics.
    json.string("sta_ip", d.staIp);
    json.string("sta_gateway", d.staGateway);
    json.string("sta_netmask", d.staNetmask);
    json.string("sta_state", d.staState);
    json.string("sta_error", d.staError);
    json.append("\"sta_rssi\":%d,", d.staRssi);
    json.number("sta_channel", d.staChannel);
    json.number("sta_disconnect_reason", d.staDisconnectReason);
    json.boolean("mdns_ready", d.mdnsReady);
    json.number("http_port", 80);
    json.number("ws_port", 81);
    json.boolean("ap_config_ready", d.apConfigReady);
    json.boolean("ap_ready", d.apReady);
    // Legacy names retained, now backed by the local TCP listener state.
    json.boolean("http_ready", d.httpListening);
    json.boolean("websocket_ready", d.websocketListening);
    json.boolean("littlefs_ready", d.littleFsReady);
    json.boolean("index_ready", d.indexReady);
    json.boolean("script_ready", d.scriptReady);
    json.boolean("style_ready", d.styleReady);
    json.boolean("control_task_ready", d.controlTaskCreated);
    json.boolean("control_heartbeat_seen", d.heartbeatSeen);
    json.number("control_heartbeat_count", d.heartbeatCount);
    json.number("control_heartbeat_ms", d.heartbeatAtMs);
    if (d.heartbeatSeen) {
        json.number("control_heartbeat_age_ms", d.uptimeMs - d.heartbeatAtMs);
    } else {
        json.append("\"control_heartbeat_age_ms\":null,");
    }
    json.boolean("control_healthy", d.controlTaskCreated &&
                 heartbeatFresh(d.heartbeatSeen, d.uptimeMs, d.heartbeatAtMs));
    json.boolean("command_queue_ready", d.commandQueueReady);
    json.boolean("config_queue_ready", d.configQueueReady);
    json.number("http_requests", d.httpRequests);
    json.number("ws_connections", d.wsConnections);
    json.number("ws_clients", d.wsClients);
    json.boolean("imu_available", d.imuAvailable);
    json.boolean("motors_enabled", false);
    json.number("state", d.state);
    json.append("\"fault\":%u}", d.fault);
    if (!json.valid()) output[0] = '\0';
    return json.valid();
}

}
