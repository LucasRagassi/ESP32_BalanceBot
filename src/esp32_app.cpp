#ifdef ARDUINO_ARCH_ESP32

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <nvs.h>
#include <esp_netif.h>
#include <esp_log.h>
#include "balancebot_build.hpp"

#include "balance/angle_estimator.hpp"
#include "balance/command_manager.hpp"
#include "balance/config.hpp"
#include "balance/diagnostics.hpp"
#include "balance/esp32_network.hpp"
#include "balance/esp32_console.hpp"
#include "balance/config_manager.hpp"
#include "balance/motion_controller.hpp"
#include "balance/pid.hpp"
#include "balance/safety.hpp"
#include "balance/snapshot_store.hpp"
#include "balance/stepper.hpp"
#include "balance/mocks.hpp"
#include "balance/mpu6050.hpp"

#include <cmath>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef BALANCEBOT_AP_SSID
#define BALANCEBOT_AP_SSID "BalanceBot-Setup"
#endif
#ifndef BALANCEBOT_AP_PASSWORD
#define BALANCEBOT_AP_PASSWORD "BalanceBot123"
#endif

namespace {

constexpr uint8_t kImuAddress = 0x68;
constexpr uint8_t kI2cSdaPin = 21;
constexpr uint8_t kI2cSclPin = 22;
constexpr uint8_t kSafetyPin = 33;
constexpr uint32_t kControlPeriodUs = 5000;
constexpr uint32_t kImuRetryIntervalMs = 5000;
constexpr uint8_t kNoController = 0xff;
constexpr uint32_t kDiagnosticIntervalMs = 10000;

// begin() returns void in these libraries. Inspect the underlying TCP listener,
// without making a loopback connection or consuming a controller slot.
class DiagnosticHttpServer final : public WebServer {
public:
    using WebServer::WebServer;
    bool listening() { return static_cast<bool>(_server); }
};
class DiagnosticWebSocketServer final : public WebSocketsServer {
public:
    using WebSocketsServer::WebSocketsServer;
    bool listening() { return _server != nullptr && static_cast<bool>(*_server); }
};
DiagnosticHttpServer httpServer(80);
DiagnosticWebSocketServer webSocket(81);
Preferences preferences;
QueueHandle_t commandQueue = nullptr;
balance::SnapshotStore snapshots;
balance::RobotConfig config;
uint8_t controllerClient = kNoController;
uint32_t controllerEpoch = 0;
bool littleFsReady = false;
bool indexPageReady = false;
bool scriptReady = false;
bool styleReady = false;
bool apConfigReady = false;
uint32_t httpRequests = 0;
uint32_t wsConnections = 0;
bool i2cReady = false;
bool accessPointReady = false;
bool httpServerReady = false;
bool webSocketReady = false;
bool controlTaskReady = false;
uint32_t lastQueueErrorLogMs = 0;
std::atomic<uint32_t> motionEpoch{1};
uint32_t networkEpochProcessed = 1;
void invalidateNetworkMotion() { motionEpoch.fetch_add(1, std::memory_order_acq_rel); }

const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt-watchdog";
    case ESP_RST_TASK_WDT: return "task-watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
    }
}

void logStage(const char* stage, const char* result, const char* detail = "") {
    // Missing IMU and routine retry/HTTP messages are DEBUG, not a critical failure.
    const bool failed = strcmp(result, "falha") == 0;
    const bool important = failed &&
        (strcmp(stage, "I2C") == 0 || strcmp(stage, "LittleFS") == 0 ||
         strncmp(stage, "WiFi.", 5) == 0 || strcmp(stage, "HTTP listener") == 0 ||
         strcmp(stage, "WebSocket listener") == 0 || strncmp(stage, "fila ", 5) == 0 ||
         strcmp(stage, "xTaskCreatePinnedToCore") == 0 || strcmp(stage, "persistencia NVS") == 0 ||
         strcmp(stage, "configuracao dos modelos") == 0 || strcmp(stage, "HTTP /health JSON") == 0);
    consoleLog(important ? balance::LogKind::Critical : balance::LogKind::Debug,
               "[%lu ms] [%s] %s: %s%s%s heap=%u min_heap=%u\n",
               static_cast<unsigned long>(millis()), important ? "CRITICO" : "DEBUG",
               stage, result, detail[0] == '\0' ? "" : " - ", detail,
               ESP.getFreeHeap(), esp_get_minimum_free_heap_size());
}

// All fixed-buffer formatting is checked, including diagnostic text.
bool checkedFormat(char* output, size_t capacity, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int length = vsnprintf(output, capacity, format, arguments);
    va_end(arguments);
    if (balance::formatFits(length, capacity)) return true;
    if (capacity > 0) output[0] = '\0';
    static std::atomic<uint32_t> lastErrorMs{UINT32_MAX - 10000};
    uint32_t previous = lastErrorMs.load(std::memory_order_relaxed);
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - previous) >= 10000 &&
        lastErrorMs.compare_exchange_strong(previous, now, std::memory_order_relaxed)) {
        consoleLog(balance::LogKind::Critical, "[JSON/FORMAT] falha required=%d capacity=%u; payload descartado\n",
                      length, static_cast<unsigned>(capacity));
    }
    return false;
}

struct ApNetwork {
    bool valid = false;
    String ip = "0.0.0.0";
    String gateway = "0.0.0.0";
    String netmask = "0.0.0.0";
};

ApNetwork readApNetwork() {
    ApNetwork network;
    esp_netif_ip_info_t info{};
    esp_netif_t* interface = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (interface != nullptr && esp_netif_get_ip_info(interface, &info) == ESP_OK) {
        network.ip = IPAddress(info.ip.addr).toString();
        network.gateway = IPAddress(info.gw.addr).toString();
        network.netmask = IPAddress(info.netmask.addr).toString();
        network.valid = info.ip.addr != 0;
    }
    return network;
}

bool assetReadable(const char* path) {
    if (!littleFsReady) return false;
    File file = LittleFS.open(path, "r");
    return file && !file.isDirectory() && file.size() > 0;
}

void inspectAssets() {
    indexPageReady = assetReadable("/index.html");
    scriptReady = assetReadable("/app.js");
    styleReady = assetReadable("/style.css");
}

void markHttpRequest(const char* path) {
    ++httpRequests;
    httpServer.sendHeader("Cache-Control", "no-store");
    httpServer.sendHeader("X-BalanceBot-Version", BALANCEBOT_VERSION);
    httpServer.sendHeader("X-BalanceBot-Build", BALANCEBOT_BUILD_UTC);
    consoleLog(balance::LogKind::Debug, "[%lu ms] [HTTP] GET %s request=%lu heap=%u\n",
                  static_cast<unsigned long>(millis()), path,
                  static_cast<unsigned long>(httpRequests), ESP.getFreeHeap());
}

void logTaskDiagnostics(const char* taskName) {
    const UBaseType_t stackFreeWords = uxTaskGetStackHighWaterMark(nullptr);
    consoleLog(balance::LogKind::Debug, "[%lu ms] [TASK] %s alive stack_free=%u heap=%u min_heap=%u\n",
                  static_cast<unsigned long>(millis()),
                  taskName, static_cast<unsigned>(stackFreeWords), ESP.getFreeHeap(),
                  esp_get_minimum_free_heap_size());
    if (stackFreeWords < 256) {
        consoleLog(balance::LogKind::Critical,
                   "[CRITICO] Watermark de stack abaixo de 256 unidades em %s.\n", taskName);
    }
}

class NvsConfigStorage final : public balance::ConfigStorage {
public:
    bool load(balance::RobotConfig& value) override {
        nvs_handle_t handle;
        const esp_err_t probe = nvs_open("balancebot", NVS_READONLY, &handle);
        if (probe == ESP_ERR_NVS_NOT_FOUND) {
            namespaceMissing_ = true;
            return false;
        }
        if (probe != ESP_OK) {
            consoleLog(balance::LogKind::Critical, "NVS probe falhou: %s\n", esp_err_to_name(probe));
            return false;
        }
        nvs_close(handle);
        if (!preferences.begin("balancebot", true)) {
            return false;
        }
        value.anglePid.kp = preferences.getFloat("pid_kp", value.anglePid.kp);
        value.anglePid.ki = preferences.getFloat("pid_ki", value.anglePid.ki);
        value.anglePid.kd = preferences.getFloat("pid_kd", value.anglePid.kd);
        value.remoteTimeoutMs = preferences.getUInt("cmd_timeout", value.remoteTimeoutMs);
        value.leftStepper.currentLimitAmps =
            preferences.getFloat("l_current", value.leftStepper.currentLimitAmps);
        value.leftStepper.maximumSpeedStepsPerSecond =
            preferences.getFloat("l_max_speed", value.leftStepper.maximumSpeedStepsPerSecond);
        value.leftStepper.accelerationStepsPerSecond2 =
            preferences.getFloat("l_accel", value.leftStepper.accelerationStepsPerSecond2);
        value.leftStepper.directionInverted = preferences.getBool("l_inverted", false);
        value.rightStepper.currentLimitAmps =
            preferences.getFloat("r_current", value.rightStepper.currentLimitAmps);
        value.rightStepper.maximumSpeedStepsPerSecond =
            preferences.getFloat("r_max_speed", value.rightStepper.maximumSpeedStepsPerSecond);
        value.rightStepper.accelerationStepsPerSecond2 =
            preferences.getFloat("r_accel", value.rightStepper.accelerationStepsPerSecond2);
        value.rightStepper.directionInverted = preferences.getBool("r_inverted", true);
        value.motorElectricalSpecsConfirmed =
            preferences.getBool("specs_confirmed", false);
        preferences.end();
        return true;
    }

    bool save(const balance::RobotConfig& value) override {
        if (!balance::validateConfig(value) || !preferences.begin("balancebot", false)) {
            return false;
        }
        const bool ok = preferences.putFloat("pid_kp", value.anglePid.kp) > 0 &&
                        preferences.putFloat("pid_ki", value.anglePid.ki) > 0 &&
                        preferences.putFloat("pid_kd", value.anglePid.kd) > 0 &&
                        preferences.putUInt("cmd_timeout", value.remoteTimeoutMs) > 0 &&
                        preferences.putFloat("l_current", value.leftStepper.currentLimitAmps) > 0 &&
                        preferences.putFloat("l_max_speed", value.leftStepper.maximumSpeedStepsPerSecond) > 0 &&
                        preferences.putFloat("l_accel", value.leftStepper.accelerationStepsPerSecond2) > 0 &&
                        preferences.putBool("l_inverted", value.leftStepper.directionInverted) &&
                        preferences.putFloat("r_current", value.rightStepper.currentLimitAmps) > 0 &&
                        preferences.putFloat("r_max_speed", value.rightStepper.maximumSpeedStepsPerSecond) > 0 &&
                        preferences.putFloat("r_accel", value.rightStepper.accelerationStepsPerSecond2) > 0 &&
                        preferences.putBool("r_inverted", value.rightStepper.directionInverted) &&
                        preferences.putBool("specs_confirmed", value.motorElectricalSpecsConfirmed);
        preferences.end();
        return ok;
    }

    bool namespaceMissing() const { return namespaceMissing_; }

private:
    bool namespaceMissing_ = false;
};

enum class RequestKind : uint8_t {
    Connect, Disconnect, SetMotion, SetMotorConfig, Arm, Disarm
};
struct RemoteRequest {
    RequestKind kind = RequestKind::Disconnect;
    float velocity = 0.0F;
    float turn = 0.0F;
    balance::StepperMotorConfig leftMotor{};
    balance::StepperMotorConfig rightMotor{};
    uint32_t epoch = 0;
};
QueueHandle_t configSaveQueue = nullptr;

bool readMpuRegister(uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(kImuAddress);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 ||
        Wire.requestFrom(kImuAddress, uint8_t(1)) != 1 || Wire.available() != 1) {
        return false;
    }
    value = static_cast<uint8_t>(Wire.read());
    return true;
}

bool writeMpuRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(kImuAddress);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool readMpu6050(balance::ImuReading& reading) {
    Wire.beginTransmission(kImuAddress);
    Wire.write(0x3B);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(kImuAddress, uint8_t(14)) != 14) {
        return false;
    }
    int16_t values[7];
    for (uint8_t index = 0; index < 7; ++index) {
        values[index] = static_cast<int16_t>((Wire.read() << 8) | Wire.read());
    }
    constexpr float accelScale = 16384.0F;
    constexpr float gyroScale = 131.0F;
    reading.accelX = static_cast<float>(values[0]) / accelScale;
    reading.accelY = static_cast<float>(values[1]) / accelScale;
    reading.accelZ = static_cast<float>(values[2]) / accelScale;
    reading.gyroXDegPerSec = static_cast<float>(values[4]) / gyroScale;
    reading.gyroYDegPerSec = static_cast<float>(values[5]) / gyroScale;
    reading.gyroZDegPerSec = static_cast<float>(values[6]) / gyroScale;
    reading.valid = std::isfinite(reading.accelX) && std::isfinite(reading.accelY) &&
                    std::isfinite(reading.accelZ) && std::isfinite(reading.gyroYDegPerSec);
    return reading.valid;
}

bool initializeMpu6050(uint8_t& identity) {
    // Address-only STOP probe: an expected NACK must not enter Wire's repeated-start read path.
    Wire.beginTransmission(kImuAddress);
    if (Wire.endTransmission(true) != 0) return false;
    if (!readMpuRegister(0x75, identity) ||
        !balance::isCompatibleMpu6050Identity(identity)) {
        return false;
    }

    constexpr uint8_t registers[] = {0x6B, 0x1A, 0x1B, 0x1C};
    constexpr uint8_t values[] = {0x00, 0x03, 0x00, 0x00};
    for (size_t index = 0; index < sizeof(registers); ++index) {
        uint8_t readback = 0;
        if (!writeMpuRegister(registers[index], values[index]) ||
            !readMpuRegister(registers[index], readback) ||
            readback != values[index]) {
            return false;
        }
    }
    uint8_t confirmedIdentity = 0;
    return readMpuRegister(0x75, confirmedIdentity) &&
           balance::isCompatibleMpu6050Identity(confirmedIdentity);
}

void enqueue(const RemoteRequest& request) {
    if (commandQueue == nullptr) {
        return;
    }
    RemoteRequest tagged = request;
    tagged.epoch = motionEpoch.load(std::memory_order_acquire);
    if (xQueueSend(commandQueue, &tagged, 0) != pdTRUE &&
        static_cast<uint32_t>(millis() - lastQueueErrorLogMs) >= 1000) {
        lastQueueErrorLogMs = millis();
        consoleLine(balance::LogKind::Transition, "Fila de comandos indisponivel ou cheia; comando descartado.");
    }
}

bool parseNumber(const char* message, const char* key, float& value) {
    char field[24];
    if (!checkedFormat(field, sizeof(field), "\"%s\"", key)) return false;
    const char* position = strstr(message, field);
    if (position == nullptr || (position = strchr(position, ':')) == nullptr) {
        return false;
    }
    char* end = nullptr;
    const float parsed = strtof(position + 1, &end);
    if (end == position + 1 || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool hasTrueValue(const char* message, const char* key) {
    char field[24];
    if (!checkedFormat(field, sizeof(field), "\"%s\"", key)) return false;
    const char* position = strstr(message, field);
    if (position == nullptr || (position = strchr(position, ':')) == nullptr) {
        return false;
    }
    position = strchr(position, ':') + 1;
    while (*position == ' ' || *position == '\t') {
        ++position;
    }
    return strncmp(position, "true", 4) == 0;
}

void webSocketEvent(uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_ERROR) {
        consoleLog(balance::LogKind::Debug, "[WS] erro client=%u payload_bytes=%u\n", client,
                      static_cast<unsigned>(length));
        return;
    }
    if (type == WStype_CONNECTED) {
        ++wsConnections;
        consoleLog(balance::LogKind::Debug, "[WS] conectado client=%u total=%lu controller=%u\n", client,
                      static_cast<unsigned long>(wsConnections), controllerClient);
        if (controllerClient != kNoController) {
            consoleLog(balance::LogKind::Debug, "[WS] recusado client=%u; controlador ocupado\n", client);
            webSocket.disconnect(client);
            return;
        }
        controllerClient = client;
        controllerEpoch = motionEpoch.load(std::memory_order_acquire);
        enqueue({RequestKind::Connect, 0.0F, 0.0F});
        return;
    }
    if (type == WStype_DISCONNECTED) {
        consoleLog(balance::LogKind::Debug, "[WS] desconectado client=%u\n", client);
        if (client == controllerClient) {
            controllerClient = kNoController;
            invalidateNetworkMotion();
            enqueue({RequestKind::Disconnect, 0.0F, 0.0F});
        }
        return;
    }
    if (client != controllerClient || controllerEpoch != motionEpoch.load(std::memory_order_acquire) ||
        type != WStype_TEXT || length == 0 ||
        length > 512) {
        return;
    }

    char message[513];
    memcpy(message, payload, length);
    message[length] = '\0';
    if (strstr(message, "\"type\":\"motion\"") != nullptr) {
        float velocity = 0.0F;
        float turn = 0.0F;
        if (parseNumber(message, "velocity", velocity) &&
            parseNumber(message, "turn", turn) && fabsf(velocity) <= 1.0F &&
            fabsf(turn) <= 1.0F) {
            enqueue({RequestKind::SetMotion, velocity, turn});
        }
    } else if (strstr(message, "\"type\":\"arm\"") != nullptr &&
               hasTrueValue(message, "armed")) {
        enqueue({RequestKind::Arm, 0.0F, 0.0F});
    } else if (strstr(message, "\"type\":\"arm\"") != nullptr) {
        enqueue({RequestKind::Disarm, 0.0F, 0.0F});
    } else if (strstr(message, "\"type\":\"motor_config\"") != nullptr) {
        RemoteRequest request;
        request.kind = RequestKind::SetMotorConfig;
        float current = 0.0F;
        float maxSpeed = 0.0F;
        float acceleration = 0.0F;
        float direction = 0.0F;
        if (parseNumber(message, "left_current_a", current)) {
            request.leftMotor.currentLimitAmps = current;
        } else {
            return;
        }
        if (!parseNumber(message, "left_max_steps_s", maxSpeed)) return;
        request.leftMotor.maximumSpeedStepsPerSecond = maxSpeed;
        if (!parseNumber(message, "left_accel_steps_s2", acceleration)) return;
        request.leftMotor.accelerationStepsPerSecond2 = acceleration;
        if (!parseNumber(message, "left_direction", direction) ||
            (direction != 1.0F && direction != -1.0F)) return;
        request.leftMotor.directionInverted = direction < 0.0F;
        if (!parseNumber(message, "right_current_a", current)) return;
        request.rightMotor.currentLimitAmps = current;
        if (!parseNumber(message, "right_max_steps_s", maxSpeed)) return;
        request.rightMotor.maximumSpeedStepsPerSecond = maxSpeed;
        if (!parseNumber(message, "right_accel_steps_s2", acceleration)) return;
        request.rightMotor.accelerationStepsPerSecond2 = acceleration;
        if (!parseNumber(message, "right_direction", direction) ||
            (direction != 1.0F && direction != -1.0F)) return;
        request.rightMotor.directionInverted = direction < 0.0F;
        enqueue(request);
    }
}

void handleRoot() {
    markHttpRequest("/");
    if (!littleFsReady || !indexPageReady) {
        static const char fallbackPage[] PROGMEM =
            "<!doctype html><html lang='pt-BR'><meta name='viewport' "
            "content='width=device-width,initial-scale=1'><title>BalanceBot DEMO</title>"
            "<h1>BalanceBot DEMO</h1><p>Interface LittleFS indisponivel.</p>"
            "<p>Use <a href='/health'>/health</a> para diagnostico JSON.</p>"
            "<p>Motores fisicos permanecem desabilitados.</p></html>";
        httpServer.send_P(200, "text/html; charset=utf-8", fallbackPage);
        logStage("HTTP /", "fallback", "200; painel LittleFS indisponivel");
        return;
    }
    File page = LittleFS.open("/index.html", "r");
    if (!page || page.isDirectory() || page.size() == 0) {
        logStage("HTTP /", "falha", "index.html deixou de estar disponivel");
        httpServer.send(503, "text/plain",
                        "index.html indisponivel; consulte /health");
        return;
    }
    const size_t expected = page.size();
    const size_t sent = httpServer.streamFile(page, "text/html; charset=utf-8");
    consoleLog(balance::LogKind::Debug, "[HTTP] / status=200 bytes=%u expected=%u complete=%u\n",
                  static_cast<unsigned>(sent), static_cast<unsigned>(expected), sent == expected);
    page.close();
}

void handleAsset(const char* path, const char* contentType) {
    markHttpRequest(httpServer.uri().c_str());
    File asset;
    if (littleFsReady) asset = LittleFS.open(path, "r");
    if (!asset || asset.isDirectory() || asset.size() == 0) {
        consoleLog(balance::LogKind::Debug, "[HTTP] %s status=404 littlefs=%u\n", path, littleFsReady);
        httpServer.send(404, "text/plain", "Arquivo nao encontrado no LittleFS");
        return;
    }
    const size_t expected = asset.size();
    const size_t sent = httpServer.streamFile(asset, contentType);
    consoleLog(balance::LogKind::Debug, "[HTTP] %s status=200 bytes=%u expected=%u complete=%u\n", path,
                  static_cast<unsigned>(sent), static_cast<unsigned>(expected), sent == expected);
    asset.close();
}

bool loadConfig() {
    NvsConfigStorage storage;
    balance::ConfigManager manager(storage);
    if (!manager.load(config)) {
        config = balance::RobotConfig{};
        logStage("NVS/configuracao", "defaults",
                 storage.namespaceMissing()
                     ? "namespace ausente; nenhuma NVS foi apagada"
                     : "leitura ausente/invalida; nenhuma NVS foi apagada");
        return false;
    }
    return true;
}

void controlTask(void*) {
    logStage("controlTask", "inicio", "controle a cada 5 ms; STEP/DIR sem hardware");
    balance::SafetyStateMachine stateMachine;
    balance::RemoteCommandManager commands(config.remoteTimeoutMs,
                                            config.maximumVelocityReference);
    balance::ComplementaryAngleEstimator estimator;
    balance::Pid anglePid(config.anglePid);
    balance::PidOuterLoopController outerLoop(
        balance::PidConfig{3.0F, 0.0F, 0.0F, -8.0F, 8.0F, -4.0F, 4.0F},
        balance::PidConfig{2.0F, 0.0F, 0.0F, -8.0F, 8.0F, -4.0F, 4.0F});
    balance::ImuReading imu;
    balance::SimulatedStepper leftStepperModel;
    balance::SimulatedStepper rightStepperModel;
    balance::StepperAxisController leftAxis(leftStepperModel);
    balance::StepperAxisController rightAxis(rightStepperModel);
    balance::MissedStepMonitor leftMissedSteps;
    balance::MissedStepMonitor rightMissedSteps;
    if (!leftAxis.configure(config.leftStepper) ||
        !rightAxis.configure(config.rightStepper)) {
        stateMachine.latchFault(balance::FaultCode::InvalidSensorReading);
        logStage("configuracao dos modelos", "falha", "perfil de motor invalido");
    } else {
        logStage("configuracao dos modelos", "sucesso");
    }
    float angle = 0.0F;
    uint32_t missedDeadlines = 0;
    uint32_t heartbeatCount = 0;
    uint32_t controlEpoch = motionEpoch.load(std::memory_order_acquire);
    stateMachine.beginCalibration();
    logStage("deteccao inicial MPU6050/WHO_AM_I", "inicio");
    uint8_t imuIdentity = 0;
    bool sensorAvailable = i2cReady && initializeMpu6050(imuIdentity) &&
                           readMpu6050(imu) && imu.valid;
    if (sensorAvailable) {
        stateMachine.finishCalibration(true);
        char detail[56];
        checkedFormat(detail, sizeof(detail), "WHO_AM_I=0x%02X; registros confirmados",
                 imuIdentity);
        logStage("deteccao inicial MPU6050", "sucesso", detail);
    } else {
        stateMachine.enterDemo(balance::FaultCode::SensorUnavailable);
        char detail[112];
        checkedFormat(detail, sizeof(detail),
                 "I2C=%s WHO_AM_I=0x%02X; DEMO seguro; retentativa em 5 s; motores OFF",
                 i2cReady ? "ready" : "failed", imuIdentity);
        logStage("deteccao inicial MPU6050", "falha", detail);
    }
    uint32_t nextImuRetryMs = millis() + kImuRetryIntervalMs;
    int64_t previousWake = esp_timer_get_time();
    int64_t nextWake = previousWake;
    TickType_t lastWakeTicks = xTaskGetTickCount();
    uint32_t lastTaskDiagnosticMs = millis();
    logStage("controlTask", "sucesso", "tarefa executando");

    for (;;) {
        nextWake += kControlPeriodUs;
        const int64_t before = esp_timer_get_time();
        const int64_t lateness = before - nextWake;
        const bool deadlineMissed = lateness > static_cast<int64_t>(kControlPeriodUs / 2);
        if (deadlineMissed) {
            ++missedDeadlines;
        }

        const auto invalidateStaleMotion = [&] {
            const uint32_t currentEpoch = motionEpoch.load(std::memory_order_acquire);
            if (currentEpoch != controlEpoch) {
                commands.setConnected(false);
                outerLoop.reset();
                controlEpoch = currentEpoch;
            }
        };
        invalidateStaleMotion();
        RemoteRequest request;
        while (xQueueReceive(commandQueue, &request, 0) == pdTRUE) {
            invalidateStaleMotion();
            if (request.epoch != controlEpoch) continue;
            switch (request.kind) {
            case RequestKind::Connect:
                commands.setConnected(true);
                break;
            case RequestKind::Disconnect:
                commands.setConnected(false);
                break;
            case RequestKind::SetMotion:
                commands.update(request.velocity, request.turn, millis());
                break;
            case RequestKind::SetMotorConfig:
                if (stateMachine.state() != balance::RobotState::Balancing) {
                    balance::RobotConfig candidate = config;
                    candidate.leftStepper = request.leftMotor;
                    candidate.rightStepper = request.rightMotor;
                    if (balance::validateConfig(candidate)) {
                        const balance::RobotConfig previous = config;
                        const bool leftConfigured = leftAxis.configure(candidate.leftStepper);
                        const bool rightConfigured =
                            leftConfigured && rightAxis.configure(candidate.rightStepper);
                        if (leftConfigured && rightConfigured) {
                            config = candidate;
                            if (configSaveQueue != nullptr &&
                                xQueueOverwrite(configSaveQueue, &config) != pdTRUE) {
                                consoleLine(balance::LogKind::Critical, "Fila de persistencia da configuracao indisponivel.");
                            }
                        } else {
                            const bool rolledBack =
                                leftAxis.configure(previous.leftStepper) &&
                                rightAxis.configure(previous.rightStepper);
                            consoleLine(rolledBack ? balance::LogKind::Transition : balance::LogKind::Critical, rolledBack
                                               ? "Configuracao de motor recusada; perfis restaurados."
                                               : "Falha critica ao restaurar perfis de motor.");
                        }
                    } else {
                        consoleLine(balance::LogKind::Debug, "Configuracao de motor fora dos limites permitidos.");
                    }
                }
                break;
            case RequestKind::Arm:
                stateMachine.requestBalance(digitalRead(kSafetyPin) == LOW, angle);
                anglePid.reset();
                outerLoop.reset();
                break;
            case RequestKind::Disarm:
                stateMachine.disarm();
                anglePid.reset();
                outerLoop.reset();
                break;
            }
        }

        const float dt = static_cast<float>(before - previousWake) / 1000000.0F;
        previousWake = before;
        const uint32_t nowMs = millis();
        bool sensorOk = false;
        if (!sensorAvailable &&
            static_cast<int32_t>(nowMs - nextImuRetryMs) >= 0) {
            logStage("retentativa MPU6050", "inicio");
            sensorAvailable = i2cReady && initializeMpu6050(imuIdentity) &&
                              readMpu6050(imu) && imu.valid;
            nextImuRetryMs = nowMs + kImuRetryIntervalMs;
            if (sensorAvailable) {
                char detail[80];
                checkedFormat(detail, sizeof(detail),
                         "WHO_AM_I=0x%02X; DEMO mantido ate reinicializacao segura",
                         imuIdentity);
                logStage("retentativa MPU6050", "sucesso", detail);
            } else {
                char detail[88];
                checkedFormat(detail, sizeof(detail),
                         "WHO_AM_I=0x%02X; proxima em 5 s; sem leituras I2C intermediarias",
                         imuIdentity);
                logStage("retentativa MPU6050", "falha", detail);
            }
        } else if (sensorAvailable) {
            sensorOk = readMpu6050(imu) && estimator.update(imu, dt, angle);
            if (!sensorOk) {
                sensorAvailable = false;
                nextImuRetryMs = nowMs + kImuRetryIntervalMs;
                logStage("MPU6050", "falha", "sensor perdido; leituras pausadas por 5 s");
                if (stateMachine.state() == balance::RobotState::Balancing) {
                    stateMachine.latchFault(balance::FaultCode::SensorUnavailable);
                } else {
                    stateMachine.enterDemo(balance::FaultCode::SensorUnavailable);
                }
            }
        }
        invalidateStaleMotion();
        const balance::MotionCommand motion = commands.current(millis());
        const bool safetyClosed = digitalRead(kSafetyPin) == LOW;
        if (!sensorOk && stateMachine.state() == balance::RobotState::Balancing) {
            stateMachine.latchFault(balance::FaultCode::SensorUnavailable);
        }
        if (stateMachine.state() == balance::RobotState::Balancing) {
            stateMachine.updateBalanceHealth(sensorOk, deadlineMissed, angle,
                                             config.maximumTiltDeg, safetyClosed);
        }
        float controlOutput = 0.0F;
        float leftVelocity = 0.0F;
        float rightVelocity = 0.0F;
        if (stateMachine.state() == balance::RobotState::Balancing && sensorOk) {
            const float angleSetpoint =
                outerLoop.velocityAngleReference(motion, 0.0F, dt);
            controlOutput = anglePid.update(angleSetpoint, angle, dt);
            const float normalized = controlOutput / 255.0F;
            const float leftDemand = normalized + motion.turn * 0.2F;
            const float rightDemand = normalized - motion.turn * 0.2F;
            leftAxis.setEnabled(true);
            rightAxis.setEnabled(true);
            leftAxis.update(leftDemand, dt);
            rightAxis.update(rightDemand, dt);
            leftStepperModel.advance(dt);
            rightStepperModel.advance(dt);
            leftVelocity = leftAxis.velocity();
            rightVelocity = rightAxis.velocity();
        } else {
            leftAxis.setEnabled(false);
            rightAxis.setEnabled(false);
            anglePid.reset();
            outerLoop.reset();
        }
        const bool leftStepsOk = leftMissedSteps.update(
            leftStepperModel.commandedSteps(), leftStepperModel.measuredSteps());
        const bool rightStepsOk = rightMissedSteps.update(
            rightStepperModel.commandedSteps(), rightStepperModel.measuredSteps());
        if (stateMachine.state() == balance::RobotState::Balancing &&
            (!leftStepsOk || !rightStepsOk)) {
            stateMachine.updateBalanceHealth(sensorOk, deadlineMissed, angle,
                                             config.maximumTiltDeg, safetyClosed,
                                             true);
        }
        if (stateMachine.state() != balance::RobotState::Balancing) {
            leftAxis.setEnabled(false);
            rightAxis.setEnabled(false);
            controlOutput = 0.0F;
            leftVelocity = 0.0F;
            rightVelocity = 0.0F;
        }
        const float executionUs = static_cast<float>(esp_timer_get_time() - before);
        const float jitterUs = fabsf(static_cast<float>(lateness));
        balance::TelemetrySnapshot snapshot;
        snapshot.timestampMs = millis();
        snapshot.controlHeartbeatCount = ++heartbeatCount;
        snapshot.controlHeartbeatSeen = true;
        snapshot.state = stateMachine.state();
        snapshot.fault = stateMachine.fault();
        snapshot.angleDeg = angle;
        snapshot.gyroRateDegPerSec = imu.gyroYDegPerSec;
        snapshot.batteryVolts = 0.0F;
        snapshot.controlOutput = controlOutput;
        snapshot.leftMotorVelocity = leftVelocity;
        snapshot.rightMotorVelocity = rightVelocity;
        snapshot.leftMotorCurrentA = config.leftStepper.currentLimitAmps;
        snapshot.rightMotorCurrentA = config.rightStepper.currentLimitAmps;
        snapshot.leftMotorMaxSpeed = config.leftStepper.maximumSpeedStepsPerSecond;
        snapshot.rightMotorMaxSpeed = config.rightStepper.maximumSpeedStepsPerSecond;
        snapshot.leftMotorAcceleration = config.leftStepper.accelerationStepsPerSecond2;
        snapshot.rightMotorAcceleration = config.rightStepper.accelerationStepsPerSecond2;
        snapshot.leftDirectionInverted = config.leftStepper.directionInverted;
        snapshot.rightDirectionInverted = config.rightStepper.directionInverted;
        snapshot.leftStepError = leftMissedSteps.difference();
        snapshot.rightStepError = rightMissedSteps.difference();
        snapshot.motorElectricalSpecsConfirmed = config.motorElectricalSpecsConfirmed;
        snapshot.controlExecutionUs = executionUs;
        snapshot.controlJitterUs = jitterUs;
        snapshot.missedDeadlines = missedDeadlines;
        snapshot.remoteConnected = motion.connected;
        snapshot.remoteCommandExpired = commands.expired();
        snapshot.imuAvailable = sensorAvailable;
        snapshots.publish(snapshot);
        if (static_cast<uint32_t>(nowMs - lastTaskDiagnosticMs) >= kDiagnosticIntervalMs) {
            lastTaskDiagnosticMs = nowMs;
            logTaskDiagnostics("balance-control");
        }
        vTaskDelayUntil(&lastWakeTicks, pdMS_TO_TICKS(5));
    }
}

void broadcastTelemetry() {
    static uint32_t lastSentMs = 0;
    const uint32_t now = millis();
    if (now - lastSentMs < 100) {
        return;
    }
    lastSentMs = now;
    const balance::TelemetrySnapshot data = snapshots.read();
    char message[640];
    const bool formatted = checkedFormat(message, sizeof(message),
             "{\"type\":\"telemetry\",\"timestamp\":%lu,\"state\":%u,"
             "\"fault\":%u,\"angle\":%.2f,\"gyro\":%.2f,"
             "\"execution_us\":%.1f,\"jitter_us\":%.1f,"
             "\"control_output\":%.2f,\"missed_deadlines\":%lu,\"remote_connected\":%s,"
             "\"remote_expired\":%s,\"left_speed\":%.1f,\"right_speed\":%.1f,"
             "\"left_step_error\":%ld,\"right_step_error\":%ld,"
             "\"motor_specs_confirmed\":%s,\"left_current_a\":%.3f,"
             "\"right_current_a\":%.3f,\"left_max_steps_s\":%.1f,"
             "\"right_max_steps_s\":%.1f,\"left_accel_steps_s2\":%.1f,"
             "\"right_accel_steps_s2\":%.1f,\"left_direction\":%d,"
             "\"right_direction\":%d,\"imu_available\":%s}",
             static_cast<unsigned long>(data.timestampMs),
             static_cast<unsigned>(data.state), static_cast<unsigned>(data.fault),
             data.angleDeg, data.gyroRateDegPerSec, data.controlExecutionUs,
             data.controlJitterUs, data.controlOutput,
             static_cast<unsigned long>(data.missedDeadlines),
             data.remoteConnected ? "true" : "false",
             data.remoteCommandExpired ? "true" : "false",
             data.leftMotorVelocity, data.rightMotorVelocity,
             static_cast<long>(data.leftStepError), static_cast<long>(data.rightStepError),
             data.motorElectricalSpecsConfirmed ? "true" : "false",
             data.leftMotorCurrentA, data.rightMotorCurrentA,
             data.leftMotorMaxSpeed, data.rightMotorMaxSpeed,
             data.leftMotorAcceleration, data.rightMotorAcceleration,
             data.leftDirectionInverted ? -1 : 1,
             data.rightDirectionInverted ? -1 : 1,
             data.imuAvailable ? "true" : "false");
    if (formatted) webSocket.broadcastTXT(message);
}

const char* consoleStateName(balance::RobotState state) {
    switch (state) {
    case balance::RobotState::Disarmed: return "DESARMADO";
    case balance::RobotState::Calibrating: return "CALIBRANDO";
    case balance::RobotState::Ready: return "PRONTO";
    case balance::RobotState::Balancing: return "EQUILIBRANDO (somente simulacao)";
    case balance::RobotState::Fault: return "FAULT / FALHA";
    case balance::RobotState::Demo: return "DEMO (armamento bloqueado)";
    }
    return "DESCONHECIDO";
}

void applyI2cLogPolicy(bool sensorAvailable) {
    static const esp_log_level_t original = esp_log_level_get("i2c");
    static int applied = -1;
    // Supported ESP-IDF per-tag API, never a global log sink or library modification.
    const esp_log_level_t target = consoleLevel() == balance::LogLevel::Debug
        ? ESP_LOG_DEBUG : (sensorAvailable ? original : ESP_LOG_NONE);
    if (applied != static_cast<int>(target)) {
        esp_log_level_set("i2c", target);
        applied = static_cast<int>(target);
    }
}

void printConsoleStatus(bool detailed) {
    const auto snapshot = snapshots.read();
    const ApNetwork ap = readApNetwork();
    balance::HealthDiagnostics network;
    balanceNetworkHealth(network);
    inspectAssets();
    Serial.printf("\n=== BalanceBot %s | %s | console %s ===\n",
                  BALANCEBOT_VERSION, BALANCEBOT_BUILD_UTC, balance::logLevelName(consoleLevel()));
    Serial.printf("Firmware commit=%s (%s); reset=%s\n", BALANCEBOT_COMMIT, BALANCEBOT_TREE_STATE,
                  resetReasonName(esp_reset_reason()));
    Serial.printf("Modo: %s | falha=%u\n", snapshot.controlHeartbeatSeen ? consoleStateName(snapshot.state) : "INICIALIZANDO / sem snapshot",
                  static_cast<unsigned>(snapshot.fault));
    Serial.println("Motores fisicos: DESABILITADOS. STEP/DIR/TMC2209 nao inicializados.");
    Serial.printf("Sensor MPU6050: %s | I2C=%s\n", snapshot.imuAvailable ? "disponivel" : "ausente/indisponivel",
                  i2cReady ? "iniciado" : "falhou");
    Serial.printf("Wi-Fi AP: driver=%s | setup_aceito=%u | SSID=%s | IP=%s/24 (IP nao comprova beacons)\n",
                  network.apReady ? "consistente/iniciado" : "NAO confirmado/indisponivel", accessPointReady,
                  WiFi.softAPSSID().c_str(), ap.ip.c_str());
    Serial.printf("Wi-Fi STA: %s | IP=%s | erro=%s\n", network.staState, network.staIp, network.staError);
    Serial.printf("HTTP :80=%s | WebSocket :81=%s | tarefa controle=%s\n", httpServer.listening() ? "escutando" : "FALHOU",
                  webSocket.listening() ? "escutando" : "FALHOU", controlTaskReady ? "criada" : "FALHOU");
    const bool portal = assetReadable("/network.html") && assetReadable("/network.js") && assetReadable("/network.css");
    Serial.printf("LittleFS: %s | painel=%s | /network=%s\n", littleFsReady ? "montado" : "FALHOU",
                  indexPageReady && scriptReady && styleReady ? "OK" : "indisponivel", portal ? "OK" : "indisponivel (gravar LittleFS)");
    if (detailed) Serial.printf("Uptime=%lu ms | heap=%u | heartbeat=%lu | idade=%lu ms\n",
        static_cast<unsigned long>(millis()), ESP.getFreeHeap(), static_cast<unsigned long>(snapshot.controlHeartbeatCount),
        static_cast<unsigned long>(millis() - snapshot.timestampMs));
    Serial.println("Android: conecte ao AP e abra http://192.168.4.1/network. Configure a rede diretamente; use somente rede confiavel.");
    if (network.staConnected) Serial.printf("Pela LAN: http://%s/network\n", network.staIp);
    if (detailed) balanceNetworkPrintDriverStatus();
    Serial.println("Comandos: status | help | log quiet | log normal | log debug (Enter).\n");
}

void serviceConsole() {
    const auto snapshot = snapshots.read();
    applyI2cLogPolicy(snapshot.imuAvailable);
    static bool bootPrinted = false;
    static bool seen = false;
    static balance::RobotState lastState = balance::RobotState::Disarmed;
    static balance::FaultCode lastFault = balance::FaultCode::None;
    static bool lastImu = false;
    if (snapshot.controlHeartbeatSeen && (!seen || snapshot.state != lastState || snapshot.fault != lastFault)) {
        consoleLog(snapshot.state == balance::RobotState::Fault ? balance::LogKind::Critical : balance::LogKind::Transition,
                   "[%s] Estado=%s falha=%u; motores fisicos OFF.\n",
                   snapshot.state == balance::RobotState::Fault ? "SEGURANCA" : "ESTADO",
                   consoleStateName(snapshot.state), static_cast<unsigned>(snapshot.fault));
        lastState = snapshot.state; lastFault = snapshot.fault;
    }
    if (seen && lastImu != snapshot.imuAvailable) {
        consoleLog(balance::LogKind::Transition, "[SENSOR] MPU6050 %s; estado=%s.\n",
                   snapshot.imuAvailable ? "detectada" : "indisponivel", consoleStateName(snapshot.state));
    }
    if (snapshot.controlHeartbeatSeen) { seen = true; lastImu = snapshot.imuAvailable; }
    if (!bootPrinted && (seen || millis() >= 2000)) {
        bootPrinted = true;
        if (consoleLevel() != balance::LogLevel::Quiet) printConsoleStatus(false);
        if (!seen) consoleLog(balance::LogKind::Critical, "[CRITICO] Controle sem heartbeat inicial; consulte status.\n");
    }
    static balance::SerialCommandParser parser;
    for (unsigned budget = 0; budget < 32 && Serial.available() > 0; ++budget) {
        switch (parser.feed(static_cast<char>(Serial.read()))) {
        case balance::ConsoleCommand::None: break;
        case balance::ConsoleCommand::Status: printConsoleStatus(true); break;
        case balance::ConsoleCommand::Help:
            Serial.println("status: estado sem senhas; help: ajuda. Rede: http://192.168.4.1/network (rede confiavel).");
            Serial.println("log quiet | log normal | log debug: muda detalhes deste boot. Padrao NORMAL. Sem comandos de motores.");
            break;
        case balance::ConsoleCommand::Quiet: consoleSetLevel(balance::LogLevel::Quiet); Serial.println("Console QUIET; falhas criticas continuam visiveis."); break;
        case balance::ConsoleCommand::Normal: consoleSetLevel(balance::LogLevel::Normal); Serial.println("Console NORMAL."); break;
        case balance::ConsoleCommand::Debug: consoleSetLevel(balance::LogLevel::Debug); Serial.println("Console DEBUG (sem senhas)."); break;
        default: Serial.println("Comando invalido ou longo; digite help. Entrada nao foi ecoada."); break;
        }
    }
    consoleDrain();
}

}

void balanceBotSetup() {
    Serial.begin(115200);
    consoleInit();
    applyI2cLogPolicy(false);
    delay(50);
    const esp_reset_reason_t resetReason = esp_reset_reason();
    logStage("reset", "detectado", resetReasonName(resetReason));
    if (resetReason == ESP_RST_PANIC || resetReason == ESP_RST_INT_WDT ||
        resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT || resetReason == ESP_RST_BROWNOUT) {
        consoleLog(balance::LogKind::Critical, "[CRITICO] Reset por %s; motores fisicos desabilitados.\n", resetReasonName(resetReason));
    }
    logStage("setup", "inicio");
    consoleLog(balance::LogKind::Debug, "[BUILD] version=%s commit=%s tree=%s build_utc=%s source_sha256=%s\n",
                  BALANCEBOT_VERSION, BALANCEBOT_COMMIT, BALANCEBOT_TREE_STATE,
                  BALANCEBOT_BUILD_UTC, BALANCEBOT_SOURCE_SHA256);

    logStage("GPIO intertravamento", "inicio");
    pinMode(kSafetyPin, INPUT_PULLUP);
    logStage("GPIO intertravamento", "sucesso");

    logStage("I2C", "inicio", "GPIO21/22; timeout de transacao 20 ms");
    i2cReady = Wire.begin(kI2cSdaPin, kI2cSclPin, 400000);
    Wire.setTimeOut(20);
    logStage("I2C", i2cReady ? "sucesso" : "falha");

    logStage("NVS/configuracao", "inicio");
    const bool nvsConfigLoaded = loadConfig();
    if (nvsConfigLoaded) {
        logStage("NVS/configuracao", "sucesso", "configuracao existente validada");
    } else {
        logStage("NVS/configuracao", "sucesso", "defaults seguros; NVS preservada");
    }

    logStage("LittleFS", "inicio");
    littleFsReady = LittleFS.begin(false);
    if (!littleFsReady) {
        logStage("LittleFS", "falha", "servidor e /health continuam disponiveis");
    } else {
        inspectAssets();
        consoleLog(balance::LogKind::Debug, "[LittleFS] index=%u app_js=%u style_css=%u total=%u used=%u\n",
                      indexPageReady, scriptReady, styleReady,
                      static_cast<unsigned>(LittleFS.totalBytes()),
                      static_cast<unsigned>(LittleFS.usedBytes()));
        logStage("LittleFS", indexPageReady && scriptReady && styleReady ? "sucesso" : "falha",
                 indexPageReady && scriptReady && styleReady
                     ? "filesystem montado; tres assets legiveis e nao vazios"
                     : "assets ausentes/vazios; verificar uploadfs; /health disponivel");
    }

    balanceNetworkPrepare(invalidateNetworkMotion);
    logStage("WiFi.mode(WIFI_AP_STA)", "inicio");
    const bool wifiModeReady = WiFi.mode(WIFI_AP_STA);
    logStage("WiFi.mode(WIFI_AP_STA)", wifiModeReady ? "sucesso" : "falha");
    const bool wifiSleepDisabled = WiFi.setSleep(false);
    logStage("WiFi.setSleep(false)", wifiSleepDisabled ? "sucesso" : "falha");
    apConfigReady = WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
                                     IPAddress(192, 168, 4, 1),
                                     IPAddress(255, 255, 255, 0));
    if (!apConfigReady) {
        logStage("WiFi.softAPConfig", "falha");
    } else {
        logStage("WiFi.softAPConfig", "sucesso", "192.168.4.1/24");
    }
    logStage("WiFi.softAP", "inicio", BALANCEBOT_AP_SSID);
    accessPointReady = WiFi.softAP(BALANCEBOT_AP_SSID, BALANCEBOT_AP_PASSWORD);
    if (!accessPointReady) {
        logStage("WiFi.softAP", "falha");
    } else {
        const String ip = WiFi.softAPIP().toString();
        logStage("WiFi.softAP", "sucesso", ip.c_str());
    }

    logStage("rotas HTTP", "inicio");
    httpServer.on("/health", HTTP_GET, [] {
        markHttpRequest("/health");
        inspectAssets();
        const balance::TelemetrySnapshot snapshot = snapshots.read();
        const ApNetwork network = readApNetwork();
        const String ssid = WiFi.softAPSSID();
        balance::HealthDiagnostics data;
        data.version = BALANCEBOT_VERSION;
        data.commit = BALANCEBOT_COMMIT;
        data.treeState = BALANCEBOT_TREE_STATE;
        data.sourceSha256 = BALANCEBOT_SOURCE_SHA256;
        data.buildUtc = BALANCEBOT_BUILD_UTC;
        data.mode = snapshot.state == balance::RobotState::Fault ? "FAULT" :
                    ((!snapshot.imuAvailable || snapshot.state == balance::RobotState::Demo) ? "DEMO" : "SAFE");
        data.resetReason = resetReasonName(esp_reset_reason());
        data.ssid = ssid.c_str();
        data.ip = network.ip.c_str();
        data.gateway = network.gateway.c_str();
        data.netmask = network.netmask.c_str();
        data.uptimeMs = millis();
        data.freeHeap = ESP.getFreeHeap();
        data.minFreeHeap = esp_get_minimum_free_heap_size();
        data.apClients = WiFi.softAPgetStationNum();
        data.apConfigReady = apConfigReady;
        // balanceNetworkHealth below supplies real event/driver-based AP readiness.
        data.httpListening = httpServer.listening();
        data.websocketListening = webSocket.listening();
        data.littleFsReady = littleFsReady;
        data.indexReady = indexPageReady;
        data.scriptReady = scriptReady;
        data.styleReady = styleReady;
        data.controlTaskCreated = controlTaskReady;
        data.heartbeatSeen = snapshot.controlHeartbeatSeen;
        data.heartbeatAtMs = snapshot.timestampMs;
        data.heartbeatCount = snapshot.controlHeartbeatCount;
        data.commandQueueReady = commandQueue != nullptr;
        data.configQueueReady = configSaveQueue != nullptr;
        data.httpRequests = httpRequests;
        data.wsConnections = wsConnections;
        data.wsClients = webSocket.connectedClients(false);
        data.imuAvailable = snapshot.imuAvailable;
        data.state = static_cast<unsigned>(snapshot.state);
        data.fault = static_cast<unsigned>(snapshot.fault);
        balanceNetworkHealth(data);
        char response[balance::kHealthJsonCapacity];
        if (!balance::renderHealthJson(data, response, sizeof(response))) {
            logStage("HTTP /health JSON", "falha", "buffer insuficiente; resposta 500");
            httpServer.send(500, "application/json",
                            "{\"ok\":false,\"error\":\"health_json_overflow\"}");
            return;
        }
        const int status = balance::healthOk(data) ? 200 : 503;
        httpServer.send(status, "application/json", response);
        consoleLog(balance::LogKind::Debug, "[HTTP] /health status=%d bytes=%u heartbeat=%lu age_ms=%lu\n",
                      status, static_cast<unsigned>(strlen(response)),
                      static_cast<unsigned long>(snapshot.controlHeartbeatCount),
                      static_cast<unsigned long>(data.uptimeMs - snapshot.timestampMs));
    });
    httpServer.on("/", HTTP_GET, handleRoot);
    httpServer.on("/network", HTTP_GET, [] {
        handleAsset("/network.html", "text/html; charset=utf-8");
    });
    httpServer.on("/network.js", HTTP_GET, [] {
        handleAsset("/network.js", "application/javascript");
    });
    httpServer.on("/network.css", HTTP_GET, [] {
        handleAsset("/network.css", "text/css");
    });
    httpServer.on("/diagnostics", HTTP_GET, [] {
        handleAsset("/diagnostics.html", "text/html; charset=utf-8");
    });
    httpServer.on("/diagnostics.css", HTTP_GET, [] {
        handleAsset("/diagnostics.css", "text/css");
    });
    httpServer.on("/diagnostics.js", HTTP_GET, [] {
        handleAsset("/diagnostics.js", "application/javascript");
    });
    httpServer.on("/style.css", HTTP_GET, [] { handleAsset("/style.css", "text/css"); });
    httpServer.on("/app.js", HTTP_GET, [] {
        handleAsset("/app.js", "application/javascript");
    });
    httpServer.onNotFound([] {
        markHttpRequest(httpServer.uri().c_str());
        httpServer.send(404, "text/plain", "Nao encontrado");
        consoleLine(balance::LogKind::Debug, "[HTTP] status=404 rota desconhecida");
    });
    logStage("rotas HTTP", "sucesso", "/health independente do LittleFS");

    balanceNetworkSetup(httpServer);

    logStage("HTTP server.begin", "inicio");
    httpServer.begin();
    httpServerReady = httpServer.listening();
    logStage("HTTP listener", httpServerReady ? "sucesso" : "falha",
             "porta 80; estado TCP local; acesso externo requer curl");

    commandQueue = xQueueCreate(8, sizeof(RemoteRequest));
    if (commandQueue == nullptr) {
        logStage("fila de comandos", "falha");
    } else {
        logStage("fila de comandos", "sucesso");
    }
    configSaveQueue = xQueueCreate(1, sizeof(balance::RobotConfig));
    if (configSaveQueue == nullptr) {
        logStage("fila de persistencia", "falha");
    } else {
        logStage("fila de persistencia", "sucesso");
    }

    logStage("WebSocket.begin", "inicio");
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    webSocketReady = webSocket.listening();
    logStage("WebSocket listener", webSocketReady ? "sucesso" : "falha",
             "porta 81; estado TCP local; handshake externo ainda nao comprovado");
    char serviceDetail[224];
    const ApNetwork network = readApNetwork();
    const String apSsid = WiFi.softAPSSID();
    checkedFormat(serviceDetail, sizeof(serviceDetail),
             "SSID=%s IP=%s gateway=%s mask=%s ip_info=%u config=%u HTTP=80 WS=81",
             apSsid.c_str(), network.ip.c_str(), network.gateway.c_str(),
             network.netmask.c_str(), network.valid, apConfigReady);
    logStage("AP/servicos", accessPointReady && apConfigReady && network.valid &&
                                httpServerReady && webSocketReady
                                ? "sucesso"
                                : "falha",
             serviceDetail);

    if (commandQueue == nullptr) {
        logStage("controlTask", "nao iniciada", "fila de comandos indisponivel");
    } else {
        logStage("xTaskCreatePinnedToCore", "inicio", "stack=6144 bytes priority=3 core=1");
        const BaseType_t taskResult = xTaskCreatePinnedToCore(
            controlTask, "balance-control", 6144, nullptr, 3, nullptr, 1);
        controlTaskReady = taskResult == pdPASS;
        if (!controlTaskReady) {
            consoleLog(balance::LogKind::Debug, "[TASK] create failed result=%ld heap=%u\n",
                          static_cast<long>(taskResult), ESP.getFreeHeap());
            logStage("xTaskCreatePinnedToCore", "falha");
        } else {
            logStage("xTaskCreatePinnedToCore", "sucesso");
        }
    }
    logStage("setup", "concluido", "HTTP/health independem da IMU; motores fisicos sempre OFF");
}

void balanceBotLoop() {
    serviceConsole();
    balanceNetworkLoop(); // Before dispatching HTTP: reject conflicting STA leases first.
    const uint32_t epoch = motionEpoch.load(std::memory_order_acquire);
    if (epoch != networkEpochProcessed) {
        networkEpochProcessed = epoch;
        // Close the controller, not its server; reconnect must start with fresh zero inputs.
        const uint8_t client = controllerClient;
        controllerClient = kNoController;
        if (client != kNoController) webSocket.disconnect(client);
    }
    httpServer.handleClient();
    webSocket.loop();
    broadcastTelemetry();
    balance::RobotConfig pendingConfig;
    if (configSaveQueue != nullptr &&
        xQueueReceive(configSaveQueue, &pendingConfig, 0) == pdTRUE) {
        NvsConfigStorage storage;
        balance::ConfigManager manager(storage);
        if (!manager.save(pendingConfig)) {
            logStage("persistencia NVS", "falha");
        } else {
            logStage("persistencia NVS", "sucesso");
        }
    }
    static uint32_t lastLoopDiagnosticMs = 0;
    const uint32_t nowMs = millis();
    if (static_cast<uint32_t>(nowMs - lastLoopDiagnosticMs) >= kDiagnosticIntervalMs) {
        lastLoopDiagnosticMs = nowMs;
        const balance::TelemetrySnapshot heartbeat = snapshots.read();
        const uint32_t heartbeatNowMs = millis();
        const ApNetwork network = readApNetwork();
        httpServerReady = httpServer.listening();
        webSocketReady = webSocket.listening();
        consoleLog(balance::LogKind::Debug, "[%lu ms] [LOOP] heap=%u min_heap=%u ap_setup=%u http=%u ws=%u "
                      "control_task=%u heartbeat_seen=%u heartbeat_count=%lu "
                      "heartbeat_age_ms=%lu control_healthy=%u clients=%u requests=%lu "
                      "SSID=%s IP=%s gateway=%s mask=%s\n",
                      static_cast<unsigned long>(nowMs), ESP.getFreeHeap(),
                      esp_get_minimum_free_heap_size(), accessPointReady,
                      httpServerReady, webSocketReady, controlTaskReady,
                      heartbeat.controlHeartbeatSeen,
                      static_cast<unsigned long>(heartbeat.controlHeartbeatCount),
                      static_cast<unsigned long>(heartbeatNowMs - heartbeat.timestampMs),
                      controlTaskReady && balance::heartbeatFresh(
                          heartbeat.controlHeartbeatSeen, heartbeatNowMs, heartbeat.timestampMs),
                      WiFi.softAPgetStationNum(), static_cast<unsigned long>(httpRequests),
                      WiFi.softAPSSID().c_str(), network.ip.c_str(),
                      network.gateway.c_str(), network.netmask.c_str());
    }
    delay(1);
}

#endif
