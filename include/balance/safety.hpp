#pragma once

#include "balance/types.hpp"

namespace balance {

class SafetyStateMachine {
public:
    bool beginCalibration();
    bool finishCalibration(bool sensorsHealthy);
    bool requestBalance(bool interlockClosed, float angleDeg);
    void disarm();
    void latchFault(FaultCode fault);
    void updateBalanceHealth(bool sensorsHealthy, bool deadlineMissed,
                             float angleDeg, float maximumTiltDeg,
                             bool interlockClosed, bool missedSteps = false);
    RobotState state() const { return state_; }
    FaultCode fault() const { return fault_; }

private:
    RobotState state_ = RobotState::Disarmed;
    FaultCode fault_ = FaultCode::None;
};

}
