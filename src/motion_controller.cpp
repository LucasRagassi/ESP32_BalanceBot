#include "balance/motion_controller.hpp"

namespace balance {

PidOuterLoopController::PidOuterLoopController(PidConfig velocityConfig,
                                               PidConfig positionConfig)
    : velocityPid_(velocityConfig), positionPid_(positionConfig) {}

float PidOuterLoopController::velocityAngleReference(const MotionCommand& command,
                                                      float measuredVelocity,
                                                      float dtSeconds) {
    return velocityPid_.update(command.velocity, measuredVelocity, dtSeconds);
}

float PidOuterLoopController::positionAngleReference(float targetPosition, float position,
                                                      float dtSeconds) {
    return positionPid_.update(targetPosition, position, dtSeconds);
}

void PidOuterLoopController::reset() {
    velocityPid_.reset();
    positionPid_.reset();
}

}
