#include "balance/safety.hpp"

#include <cmath>

namespace balance {

bool SafetyStateMachine::beginCalibration() {
    if (state_ != RobotState::Disarmed) {
        return false;
    }
    state_ = RobotState::Calibrating;
    return true;
}

bool SafetyStateMachine::finishCalibration(bool sensorsHealthy) {
    if (state_ != RobotState::Calibrating) {
        return false;
    }
    if (!sensorsHealthy) {
        latchFault(FaultCode::SensorUnavailable);
        return false;
    }
    state_ = RobotState::Ready;
    return true;
}

void SafetyStateMachine::enterDemo(FaultCode reason) {
    if (state_ == RobotState::Disarmed || state_ == RobotState::Calibrating ||
        state_ == RobotState::Ready) {
        state_ = RobotState::Demo;
        fault_ = reason;
    }
}

bool SafetyStateMachine::requestBalance(bool interlockClosed, float angleDeg) {
    if (state_ != RobotState::Ready) {
        return false;
    }
    if (!interlockClosed) {
        latchFault(FaultCode::SafetyInterlock);
        return false;
    }
    if (!std::isfinite(angleDeg)) {
        latchFault(FaultCode::InvalidSensorReading);
        return false;
    }
    state_ = RobotState::Balancing;
    return true;
}

void SafetyStateMachine::disarm() {
    if (state_ != RobotState::Fault && state_ != RobotState::Demo) {
        state_ = RobotState::Disarmed;
        fault_ = FaultCode::None;
    }
}

void SafetyStateMachine::latchFault(FaultCode fault) {
    if (fault != FaultCode::None) {
        state_ = RobotState::Fault;
        fault_ = fault;
    }
}

void SafetyStateMachine::updateBalanceHealth(bool sensorsHealthy, bool deadlineMissed,
                                              float angleDeg, float maximumTiltDeg,
                                              bool interlockClosed, bool missedSteps) {
    if (state_ != RobotState::Balancing) {
        return;
    }
    if (!interlockClosed) {
        latchFault(FaultCode::SafetyInterlock);
    } else if (!sensorsHealthy) {
        latchFault(FaultCode::InvalidSensorReading);
    } else if (deadlineMissed) {
        latchFault(FaultCode::ControlDeadline);
    } else if (missedSteps) {
        latchFault(FaultCode::MissedSteps);
    } else if (!std::isfinite(angleDeg) || !std::isfinite(maximumTiltDeg) ||
               maximumTiltDeg <= 0.0F || std::fabs(angleDeg) > maximumTiltDeg) {
        latchFault(FaultCode::ExcessiveTilt);
    }
}

}
