#pragma once

#include "balance/stepper.hpp"

#ifdef ARDUINO_ARCH_ESP32
#include <driver/rmt.h>

namespace balance {

class Esp32RmtStepPulseEngine final : public StepPulseEngine {
public:
    explicit Esp32RmtStepPulseEngine(rmt_channel_t channel);
    bool begin(uint8_t stepPin, uint8_t directionPin) override;
    bool setDirection(bool reverse) override;
    bool setFrequency(uint32_t stepsPerSecond) override;
    void stop() override;

private:
    rmt_channel_t channel_;
    uint8_t directionPin_ = 0;
    bool initialized_ = false;
    bool transmitting_ = false;
};

}
#endif
