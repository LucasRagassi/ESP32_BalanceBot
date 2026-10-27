#pragma once

#include "balance/config.hpp"

#include <cstdint>

namespace balance {

class StepPulseEngine {
public:
    virtual ~StepPulseEngine() = default;
    virtual bool begin(uint8_t stepPin, uint8_t directionPin) = 0;
    virtual bool setDirection(bool reverse) = 0;
    virtual bool setFrequency(uint32_t stepsPerSecond) = 0;
    virtual void stop() = 0;
};

class StepperDriver {
public:
    virtual ~StepperDriver() = default;
    virtual bool configure(const StepperMotorConfig& config) = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual bool setVelocity(float stepsPerSecond) = 0;
    virtual bool enabled() const = 0;
};

class StepperAxisController {
public:
    explicit StepperAxisController(StepperDriver& driver);
    bool configure(const StepperMotorConfig& config);
    void setEnabled(bool enabled);
    bool update(float normalizedDemand, float dtSeconds);
    float velocity() const { return velocity_; }
    float targetVelocity() const { return targetVelocity_; }
    const StepperMotorConfig& config() const { return config_; }

private:
    StepperDriver& driver_;
    StepperMotorConfig config_{};
    float velocity_ = 0.0F;
    float targetVelocity_ = 0.0F;
};

class MissedStepMonitor {
public:
    explicit MissedStepMonitor(int32_t maximumDifference = 32);
    bool update(int32_t commandedSteps, int32_t measuredSteps);
    void reset();
    int32_t difference() const { return difference_; }

private:
    int32_t maximumDifference_;
    int32_t difference_ = 0;
};

}
