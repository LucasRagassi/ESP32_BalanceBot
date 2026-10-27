#pragma once

#include "balance/types.hpp"

namespace balance {

class ComplementaryAngleEstimator {
public:
    explicit ComplementaryAngleEstimator(float accelerometerWeight = 0.02F);
    bool update(const ImuReading& reading, float dtSeconds, float& angleDeg);
    void reset(float angleDeg = 0.0F);

private:
    float accelerometerWeight_;
    float angleDeg_ = 0.0F;
    bool initialized_ = false;
};

}
