#ifdef ARDUINO_ARCH_ESP32

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_timer.h>

#include "balance/angle_estimator.hpp"
#include "balance/command_manager.hpp"
#include "balance/config.hpp"
#include "balance/config_manager.hpp"
#include "balance/motion_controller.hpp"
#include "balance/pid.hpp"
#include "balance/safety.hpp"
#include "balance/snapshot_store.hpp"
#include "balance/stepper.hpp"
#include "balance/mocks.hpp"

#include <cmath>
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
constexpr uint8_t kNoController = 0xff;

WebServer httpServer(80);
WebSocketsServer webSocket(81);
Preferences preferences;
QueueHandle_t commandQueue = nullptr;
balance::SnapshotStore snapshots;
balance::RobotConfig config;
uint8_t controllerClient = kNoController;

class NvsConfigStorage final : public balance::ConfigStorage {
public:
    bool load(balance::RobotConfig& value) override {
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
};
QueueHandle_t configSaveQueue = nullptr;

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

bool initializeMpu6050() {
    Wire.beginTransmission(kImuAddress);
    Wire.write(0x6B);
    Wire.write(0x00);
    return Wire.endTransmission() == 0;
}

void enqueue(const RemoteRequest& request) {
    if (commandQueue == nullptr || xQueueSend(commandQueue, &request, 0) != pdTRUE) {
        Serial.println("Fila de comandos indisponivel ou cheia; comando descartado.");
    }
}

bool parseNumber(const char* message, const char* key, float& value) {
    char field[24];
    snprintf(field, sizeof(field), "\"%s\"", key);
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
    snprintf(field, sizeof(field), "\"%s\"", key);
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
    if (type == WStype_CONNECTED) {
        if (controllerClient != kNoController) {
            webSocket.disconnect(client);
            return;
        }
        controllerClient = client;
        enqueue({RequestKind::Connect, 0.0F, 0.0F});
        return;
    }
    if (type == WStype_DISCONNECTED) {
        if (client == controllerClient) {
            controllerClient = kNoController;
            enqueue({RequestKind::Disconnect, 0.0F, 0.0F});
        }
        return;
    }
    if (client != controllerClient || type != WStype_TEXT || length == 0 ||
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
    File page = LittleFS.open("/index.html", "r");
    if (!page) {
        httpServer.send(500, "text/plain", "LittleFS: index.html nao encontrado");
        return;
    }
    httpServer.streamFile(page, "text/html; charset=utf-8");
    page.close();
}

void handleAsset(const char* path, const char* contentType) {
    File asset = LittleFS.open(path, "r");
    if (!asset) {
        httpServer.send(404, "text/plain", "Arquivo nao encontrado no LittleFS");
        return;
    }
    httpServer.streamFile(asset, contentType);
    asset.close();
}

void loadConfig() {
    NvsConfigStorage storage;
    balance::ConfigManager manager(storage);
    if (!manager.load(config)) {
        Serial.println("Configuracao NVS ausente/invalida; usando valores padrao.");
        config = balance::RobotConfig{};
    }
}

void controlTask(void*) {
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
    }
    float angle = 0.0F;
    uint32_t missedDeadlines = 0;
    stateMachine.beginCalibration();
    const bool sensorAvailable = initializeMpu6050() && readMpu6050(imu) && imu.valid;
    if (!stateMachine.finishCalibration(sensorAvailable)) {
        Serial.println("Falha critica: MPU6050 indisponivel; controle permanece em FALHA.");
    }
    int64_t previousWake = esp_timer_get_time();
    int64_t nextWake = previousWake;
    TickType_t lastWakeTicks = xTaskGetTickCount();

    for (;;) {
        nextWake += kControlPeriodUs;
        const int64_t before = esp_timer_get_time();
        const int64_t lateness = before - nextWake;
        const bool deadlineMissed = lateness > static_cast<int64_t>(kControlPeriodUs / 2);
        if (deadlineMissed) {
            ++missedDeadlines;
        }

        RemoteRequest request;
        while (xQueueReceive(commandQueue, &request, 0) == pdTRUE) {
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
                                Serial.println("Fila de persistencia da configuracao indisponivel.");
                            }
                        } else {
                            const bool rolledBack =
                                leftAxis.configure(previous.leftStepper) &&
                                rightAxis.configure(previous.rightStepper);
                            Serial.println(rolledBack
                                               ? "Configuracao de motor recusada; perfis restaurados."
                                               : "Falha critica ao restaurar perfis de motor.");
                        }
                    } else {
                        Serial.println("Configuracao de motor fora dos limites permitidos.");
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
        const bool sensorOk = readMpu6050(imu) &&
                              estimator.update(imu, dt, angle);
        const balance::MotionCommand motion = commands.current(millis());
        const bool safetyClosed = digitalRead(kSafetyPin) == LOW;
        if (!sensorOk && stateMachine.state() != balance::RobotState::Fault) {
            stateMachine.latchFault(balance::FaultCode::InvalidSensorReading);
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
        snapshots.publish(snapshot);
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
    snprintf(message, sizeof(message),
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
             "\"right_direction\":%d}",
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
             data.rightDirectionInverted ? -1 : 1);
    webSocket.broadcastTXT(message);
}

}

void balanceBotSetup() {
    Serial.begin(115200);
    pinMode(kSafetyPin, INPUT_PULLUP);
    Wire.begin(kI2cSdaPin, kI2cSclPin, 400000);
    loadConfig();
    if (!LittleFS.begin(false)) {
        Serial.println("LittleFS indisponivel; envie a imagem de filesystem antes de acessar o painel.");
    }

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(BALANCEBOT_AP_SSID, BALANCEBOT_AP_PASSWORD)) {
        Serial.println("Erro ao iniciar ponto de acesso Wi-Fi.");
    }
    Serial.printf("AP: %s | IP: %s\n", BALANCEBOT_AP_SSID,
                  WiFi.softAPIP().toString().c_str());

    httpServer.on("/", HTTP_GET, handleRoot);
    httpServer.on("/style.css", HTTP_GET, [] { handleAsset("/style.css", "text/css"); });
    httpServer.on("/app.js", HTTP_GET, [] {
        handleAsset("/app.js", "application/javascript");
    });
    httpServer.onNotFound([] { httpServer.send(404, "text/plain", "Nao encontrado"); });
    httpServer.begin();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    commandQueue = xQueueCreate(8, sizeof(RemoteRequest));
    if (commandQueue == nullptr) {
        Serial.println("Erro critico: fila de comandos nao criada.");
        return;
    }
    configSaveQueue = xQueueCreate(1, sizeof(balance::RobotConfig));
    if (configSaveQueue == nullptr) {
        Serial.println("Erro: fila de persistencia nao criada; configuracao web indisponivel.");
    }
    if (xTaskCreatePinnedToCore(controlTask, "balance-control", 6144, nullptr, 3,
                                nullptr, 1) != pdPASS) {
        Serial.println("Erro critico: tarefa de controle nao iniciada.");
    }
    Serial.println("Motores: desabilitados por projeto nesta fase.");
}

void balanceBotLoop() {
    httpServer.handleClient();
    webSocket.loop();
    broadcastTelemetry();
    balance::RobotConfig pendingConfig;
    if (configSaveQueue != nullptr &&
        xQueueReceive(configSaveQueue, &pendingConfig, 0) == pdTRUE) {
        NvsConfigStorage storage;
        balance::ConfigManager manager(storage);
        if (!manager.save(pendingConfig)) {
            Serial.println("Falha ao persistir configuracao de motores na NVS.");
        }
    }
}

#endif
