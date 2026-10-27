#include "balance/config.hpp"

#include <cmath>

namespace balance {

bool validateConfig(const RobotConfig& config) {
    const PidConfig& pid = config.anglePid;
    const auto validStepper = [](const StepperMotorConfig& motor) {
        return std::isfinite(motor.currentLimitAmps) &&
               motor.currentLimitAmps >= 0.0F && motor.currentLimitAmps <= 2.0F &&
               std::isfinite(motor.maximumSpeedStepsPerSecond) &&
               motor.maximumSpeedStepsPerSecond > 0.0F &&
               motor.maximumSpeedStepsPerSecond <= 20000.0F &&
               std::isfinite(motor.accelerationStepsPerSecond2) &&
               motor.accelerationStepsPerSecond2 > 0.0F &&
               motor.accelerationStepsPerSecond2 <= 100000.0F;
    };
    return std::isfinite(pid.kp) && std::isfinite(pid.ki) &&
           std::isfinite(pid.kd) && std::isfinite(pid.outputMin) &&
           std::isfinite(pid.outputMax) && std::isfinite(pid.integralMin) &&
           std::isfinite(pid.integralMax) && pid.outputMin < pid.outputMax &&
           pid.integralMin <= pid.integralMax &&
           std::isfinite(config.maximumTiltDeg) && config.maximumTiltDeg > 0.0F &&
           config.maximumTiltDeg <= 60.0F &&
           std::isfinite(config.maximumVelocityReference) &&
           config.maximumVelocityReference > 0.0F &&
           config.maximumVelocityReference <= 1.0F &&
           validStepper(config.leftStepper) && validStepper(config.rightStepper) &&
           config.remoteTimeoutMs >= 100 && config.remoteTimeoutMs <= 5000 &&
           config.controlFrequencyHz == 200;
}

}
