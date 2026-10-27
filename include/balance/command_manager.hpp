#pragma once

#include "balance/types.hpp"

namespace balance {

class RemoteCommandManager {
public:
    explicit RemoteCommandManager(uint32_t timeoutMs = 500,
                                  float maximumReference = 1.0F);
    bool update(float velocity, float turn, uint32_t nowMs);
    void setConnected(bool connected);
    MotionCommand current(uint32_t nowMs);
    bool expired() const { return expired_; }

private:
    uint32_t timeoutMs_;
    float maximumReference_;
    MotionCommand command_;
    bool expired_ = false;
};

}
