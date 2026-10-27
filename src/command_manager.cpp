#include "balance/command_manager.hpp"

#include <cmath>

namespace balance {

RemoteCommandManager::RemoteCommandManager(uint32_t timeoutMs, float maximumReference)
    : timeoutMs_(timeoutMs == 0 ? 1 : timeoutMs),
      maximumReference_(std::isfinite(maximumReference) && maximumReference > 0.0F &&
                                maximumReference <= 1.0F
                            ? maximumReference
                            : 1.0F) {}

bool RemoteCommandManager::update(float velocity, float turn, uint32_t nowMs) {
    if (!std::isfinite(velocity) || !std::isfinite(turn) ||
        std::fabs(velocity) > maximumReference_ ||
        std::fabs(turn) > maximumReference_) {
        return false;
    }
    command_.velocity = velocity;
    command_.turn = turn;
    command_.receivedAtMs = nowMs;
    command_.connected = true;
    expired_ = false;
    return true;
}

void RemoteCommandManager::setConnected(bool connected) {
    command_.connected = connected;
    if (!connected) {
        command_.velocity = 0.0F;
        command_.turn = 0.0F;
    }
}

MotionCommand RemoteCommandManager::current(uint32_t nowMs) {
    if (command_.connected &&
        static_cast<uint32_t>(nowMs - command_.receivedAtMs) >= timeoutMs_) {
        command_.velocity = 0.0F;
        command_.turn = 0.0F;
        command_.connected = false;
        expired_ = true;
    }
    return command_;
}

}
