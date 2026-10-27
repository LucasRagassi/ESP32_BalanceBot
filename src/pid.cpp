#include "balance/pid.hpp"

#include <cmath>

namespace balance {

namespace {
bool valid(const PidConfig& config) {
    return std::isfinite(config.kp) && std::isfinite(config.ki) &&
           std::isfinite(config.kd) && std::isfinite(config.outputMin) &&
           std::isfinite(config.outputMax) && std::isfinite(config.integralMin) &&
           std::isfinite(config.integralMax) && config.outputMin < config.outputMax &&
           config.integralMin <= config.integralMax;
}
}

Pid::Pid(PidConfig config) {
    if (valid(config)) {
        config_ = config;
    }
}

float Pid::update(float setpoint, float measurement, float dtSeconds) {
    if (!std::isfinite(setpoint) || !std::isfinite(measurement) ||
        !std::isfinite(dtSeconds) || dtSeconds <= 0.0F) {
        return 0.0F;
    }

    const float error = setpoint - measurement;
    const float derivative = hasPrevious_ ? (error - previousError_) / dtSeconds : 0.0F;
    const float candidateIntegral = integral_ + error * dtSeconds;
    const float candidateOutput = config_.kp * error + config_.ki * candidateIntegral +
                                 config_.kd * derivative;
    if (candidateOutput >= config_.outputMin && candidateOutput <= config_.outputMax) {
        integral_ = candidateIntegral;
    }
    if (config_.ki != 0.0F) {
        integral_ = integral_ < config_.integralMin
                        ? config_.integralMin
                        : (integral_ > config_.integralMax ? config_.integralMax : integral_);
    }

    previousError_ = error;
    hasPrevious_ = true;
    const float output = config_.kp * error + config_.ki * integral_ + config_.kd * derivative;
    return output < config_.outputMin
               ? config_.outputMin
               : (output > config_.outputMax ? config_.outputMax : output);
}

void Pid::reset() {
    integral_ = 0.0F;
    previousError_ = 0.0F;
    hasPrevious_ = false;
}

bool Pid::setConfig(const PidConfig& config) {
    if (!valid(config)) {
        return false;
    }
    config_ = config;
    reset();
    return true;
}

}
