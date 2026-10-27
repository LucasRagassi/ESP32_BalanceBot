#pragma once

#include "balance/pid.hpp"

#include <cstdint>

namespace balance {

struct StepperMotorConfig {
    float currentLimitAmps = 0.0F;
    float maximumSpeedStepsPerSecond = 1200.0F;
    float accelerationStepsPerSecond2 = 4000.0F;
    bool directionInverted = false;
};

struct RobotConfig {
    PidConfig anglePid{18.0F, 0.0F, 0.6F, -255.0F, 255.0F, -100.0F, 100.0F};
    StepperMotorConfig leftStepper{0.0F, 900.0F, 3000.0F, false};
    StepperMotorConfig rightStepper{0.0F, 1200.0F, 4000.0F, true};
    float maximumTiltDeg = 35.0F;
    float maximumVelocityReference = 1.0F;
    uint32_t remoteTimeoutMs = 500;
    uint8_t controlFrequencyHz = 200;
    bool motorElectricalSpecsConfirmed = false;
};

class ConfigStorage {
public:
    virtual ~ConfigStorage() = default;
    virtual bool load(RobotConfig& config) = 0;
    virtual bool save(const RobotConfig& config) = 0;
};

bool validateConfig(const RobotConfig& config);

}
