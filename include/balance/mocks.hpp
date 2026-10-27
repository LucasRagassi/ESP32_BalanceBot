#pragma once

#include "balance/drivers.hpp"
#include "balance/stepper.hpp"

#include <cmath>

namespace balance {

class MockImu final : public ImuDriver {
public:
    bool begin() override { return available_; }
    bool read(ImuReading& reading) override {
        reading = reading_;
        return available_ && reading_.valid;
    }
    void setReading(const ImuReading& reading) { reading_ = reading; }
    void setAvailable(bool available) { available_ = available; }

private:
    ImuReading reading_{0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, true};
    bool available_ = true;
};

class MockEncoder final : public EncoderDriver {
public:
    int32_t count() const override { return count_; }
    void reset() override { count_ = 0; }
    void addCounts(int32_t delta) { count_ += delta; }

private:
    int32_t count_ = 0;
};

class SimulatedStepper final : public StepperDriver {
public:
    bool configure(const StepperMotorConfig& config) override {
        config_ = config;
        return std::isfinite(config.currentLimitAmps) &&
               std::isfinite(config.maximumSpeedStepsPerSecond) &&
               config.maximumSpeedStepsPerSecond > 0.0F &&
               std::isfinite(config.accelerationStepsPerSecond2) &&
               config.accelerationStepsPerSecond2 > 0.0F;
    }
    void setEnabled(bool enabled) override {
        enabled_ = enabled;
        if (!enabled) velocity_ = 0.0F;
    }
    bool setVelocity(float stepsPerSecond) override {
        if (!enabled_ || !std::isfinite(stepsPerSecond) ||
            std::fabs(stepsPerSecond) > config_.maximumSpeedStepsPerSecond) {
            return false;
        }
        velocity_ = stepsPerSecond;
        return true;
    }
    bool enabled() const override { return enabled_; }

    void advance(float dtSeconds) {
        if (!enabled_ || !std::isfinite(dtSeconds) || dtSeconds <= 0.0F) return;
        const float delta = velocity_ * dtSeconds;
        commandedPosition_ += delta;
        measuredPosition_ += delta * (1.0F - missedStepFraction_);
    }
    void simulateMissedSteps(float fraction) {
        missedStepFraction_ = std::isfinite(fraction) && fraction >= 0.0F && fraction <= 1.0F
                                  ? fraction
                                  : 1.0F;
    }
    int32_t commandedSteps() const { return static_cast<int32_t>(commandedPosition_); }
    int32_t measuredSteps() const { return static_cast<int32_t>(measuredPosition_); }
    float velocity() const { return velocity_; }
    const StepperMotorConfig& config() const { return config_; }

private:
    StepperMotorConfig config_{};
    float velocity_ = 0.0F;
    float missedStepFraction_ = 0.0F;
    float commandedPosition_ = 0.0F;
    float measuredPosition_ = 0.0F;
    bool enabled_ = false;
};

}
