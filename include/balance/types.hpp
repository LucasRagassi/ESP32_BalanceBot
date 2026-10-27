#pragma once

#include <cstdint>

namespace balance {

enum class RobotState : uint8_t {
    Disarmed,
    Calibrating,
    Ready,
    Balancing,
    Fault
};

enum class FaultCode : uint8_t {
    None,
    SensorUnavailable,
    InvalidSensorReading,
    ControlDeadline,
    ExcessiveTilt,
    SafetyInterlock,
    MissedSteps
};

struct MotionCommand {
    float velocity = 0.0F;
    float turn = 0.0F;
    uint32_t receivedAtMs = 0;
    bool connected = false;
};

struct TelemetrySnapshot {
    uint32_t timestampMs = 0;
    RobotState state = RobotState::Disarmed;
    FaultCode fault = FaultCode::None;
    float angleDeg = 0.0F;
    float gyroRateDegPerSec = 0.0F;
    float leftEncoder = 0.0F;
    float rightEncoder = 0.0F;
    float batteryVolts = 0.0F;
    float controlOutput = 0.0F;
    float leftMotorVelocity = 0.0F;
    float rightMotorVelocity = 0.0F;
    float leftMotorCurrentA = 0.0F;
    float rightMotorCurrentA = 0.0F;
    float leftMotorMaxSpeed = 0.0F;
    float rightMotorMaxSpeed = 0.0F;
    float leftMotorAcceleration = 0.0F;
    float rightMotorAcceleration = 0.0F;
    int32_t leftStepError = 0;
    int32_t rightStepError = 0;
    bool leftDirectionInverted = false;
    bool rightDirectionInverted = false;
    bool motorElectricalSpecsConfirmed = false;
    float controlExecutionUs = 0.0F;
    float controlJitterUs = 0.0F;
    uint32_t missedDeadlines = 0;
    bool remoteConnected = false;
    bool remoteCommandExpired = false;
};

struct ImuReading {
    float accelX = 0.0F;
    float accelY = 0.0F;
    float accelZ = 0.0F;
    float gyroXDegPerSec = 0.0F;
    float gyroYDegPerSec = 0.0F;
    float gyroZDegPerSec = 0.0F;
    bool valid = false;
};

}
