#pragma once

#include "balance/command_manager.hpp"
#include "balance/pid.hpp"

namespace balance {

class OuterLoopController {
public:
    virtual ~OuterLoopController() = default;
    virtual float velocityAngleReference(const MotionCommand& command,
                                         float measuredVelocity, float dtSeconds) = 0;
    virtual float positionAngleReference(float targetPosition, float position,
                                         float dtSeconds) = 0;
    virtual void reset() = 0;
};

class PidOuterLoopController final : public OuterLoopController {
public:
    PidOuterLoopController(PidConfig velocityConfig, PidConfig positionConfig);
    float velocityAngleReference(const MotionCommand& command,
                                 float measuredVelocity, float dtSeconds) override;
    float positionAngleReference(float targetPosition, float position,
                                 float dtSeconds) override;
    void reset() override;

private:
    Pid velocityPid_;
    Pid positionPid_;
};

}
