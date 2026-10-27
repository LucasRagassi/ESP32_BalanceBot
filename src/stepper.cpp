#include "balance/stepper.hpp"

#include <cmath>
#include <cstdlib>

namespace balance {

namespace {
bool validMotorConfig(const StepperMotorConfig& config) {
    return std::isfinite(config.currentLimitAmps) &&
           config.currentLimitAmps >= 0.0F && config.currentLimitAmps <= 2.0F &&
           std::isfinite(config.maximumSpeedStepsPerSecond) &&
           config.maximumSpeedStepsPerSecond > 0.0F &&
           config.maximumSpeedStepsPerSecond <= 20000.0F &&
           std::isfinite(config.accelerationStepsPerSecond2) &&
           config.accelerationStepsPerSecond2 > 0.0F &&
           config.accelerationStepsPerSecond2 <= 100000.0F;
}
}

StepperAxisController::StepperAxisController(StepperDriver& driver) : driver_(driver) {}

bool StepperAxisController::configure(const StepperMotorConfig& config) {
    if (driver_.enabled() || !validMotorConfig(config) || !driver_.configure(config)) {
        return false;
    }
    config_ = config;
    velocity_ = 0.0F;
    targetVelocity_ = 0.0F;
    return true;
}

void StepperAxisController::setEnabled(bool enabled) {
    if (!enabled) {
        targetVelocity_ = 0.0F;
        velocity_ = 0.0F;
        driver_.setVelocity(0.0F);
    }
    driver_.setEnabled(enabled);
}

bool StepperAxisController::update(float normalizedDemand, float dtSeconds) {
    if (!driver_.enabled() || !std::isfinite(normalizedDemand) ||
        !std::isfinite(dtSeconds) || dtSeconds <= 0.0F || dtSeconds > 0.1F) {
        return false;
    }

    const float boundedDemand = normalizedDemand < -1.0F
                                    ? -1.0F
                                    : (normalizedDemand > 1.0F ? 1.0F : normalizedDemand);
    targetVelocity_ = boundedDemand * config_.maximumSpeedStepsPerSecond;
    const float maximumDelta = config_.accelerationStepsPerSecond2 * dtSeconds;
    const float difference = targetVelocity_ - velocity_;
    velocity_ += difference < -maximumDelta
                     ? -maximumDelta
                     : (difference > maximumDelta ? maximumDelta : difference);
    if (config_.directionInverted) {
        velocity_ = -velocity_;
    }
    const bool accepted = driver_.setVelocity(velocity_);
    if (config_.directionInverted) {
        velocity_ = -velocity_;
    }
    return accepted;
}

MissedStepMonitor::MissedStepMonitor(int32_t maximumDifference)
    : maximumDifference_(maximumDifference > 0 ? maximumDifference : 1) {}

bool MissedStepMonitor::update(int32_t commandedSteps, int32_t measuredSteps) {
    const int64_t delta = static_cast<int64_t>(commandedSteps) - measuredSteps;
    difference_ = static_cast<int32_t>(delta > INT32_MAX
                                           ? INT32_MAX
                                           : (delta < INT32_MIN ? INT32_MIN : delta));
    return std::abs(static_cast<int64_t>(difference_)) <= maximumDifference_;
}

void MissedStepMonitor::reset() {
    difference_ = 0;
}

}
