#include "balance/angle_estimator.hpp"

#include <cmath>

namespace balance {

ComplementaryAngleEstimator::ComplementaryAngleEstimator(float accelerometerWeight)
    : accelerometerWeight_(accelerometerWeight) {
    if (!std::isfinite(accelerometerWeight_) || accelerometerWeight_ < 0.0F ||
        accelerometerWeight_ > 1.0F) {
        accelerometerWeight_ = 0.02F;
    }
}

bool ComplementaryAngleEstimator::update(const ImuReading& reading, float dtSeconds,
                                          float& angleDeg) {
    if (!reading.valid || !std::isfinite(reading.accelX) ||
        !std::isfinite(reading.accelY) || !std::isfinite(reading.accelZ) ||
        !std::isfinite(reading.gyroYDegPerSec) || !std::isfinite(dtSeconds) ||
        dtSeconds <= 0.0F) {
        return false;
    }

    const float accelAngle = std::atan2(reading.accelX, reading.accelZ) *
                             57.2957795F;
    if (!initialized_) {
        angleDeg_ = accelAngle;
        initialized_ = true;
    } else {
        angleDeg_ = (1.0F - accelerometerWeight_) *
                        (angleDeg_ + reading.gyroYDegPerSec * dtSeconds) +
                    accelerometerWeight_ * accelAngle;
    }
    angleDeg = angleDeg_;
    return std::isfinite(angleDeg_);
}

void ComplementaryAngleEstimator::reset(float angleDeg) {
    angleDeg_ = angleDeg;
    initialized_ = std::isfinite(angleDeg);
}

}
