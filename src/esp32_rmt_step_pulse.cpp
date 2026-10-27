#ifdef ARDUINO_ARCH_ESP32

#include "balance/esp32_rmt_step_pulse.hpp"

#include <Arduino.h>

namespace balance {

Esp32RmtStepPulseEngine::Esp32RmtStepPulseEngine(rmt_channel_t channel)
    : channel_(channel) {}

bool Esp32RmtStepPulseEngine::begin(uint8_t stepPin, uint8_t directionPin) {
    if (initialized_) {
        return false;
    }

    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(static_cast<gpio_num_t>(stepPin), channel_);
    config.clk_div = 80;
    config.tx_config.loop_en = true;
    config.tx_config.carrier_en = false;
    config.tx_config.idle_output_en = true;
    config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    if (rmt_config(&config) != ESP_OK || rmt_driver_install(channel_, 0, 0) != ESP_OK) {
        return false;
    }

    directionPin_ = directionPin;
    pinMode(directionPin_, OUTPUT);
    digitalWrite(directionPin_, LOW);
    initialized_ = true;
    return true;
}

bool Esp32RmtStepPulseEngine::setDirection(bool reverse) {
    if (!initialized_ || transmitting_) {
        return false;
    }
    digitalWrite(directionPin_, reverse ? HIGH : LOW);
    return true;
}

bool Esp32RmtStepPulseEngine::setFrequency(uint32_t stepsPerSecond) {
    if (!initialized_ || stepsPerSecond > 100000U) {
        return false;
    }
    stop();
    if (stepsPerSecond == 0) {
        return true;
    }

    const uint32_t periodTicks = 1000000U / stepsPerSecond;
    if (periodTicks <= 3U || periodTicks - 3U > 32767U) {
        return false;
    }
    rmt_item32_t pulse{};
    pulse.level0 = 1;
    pulse.duration0 = 3;
    pulse.level1 = 0;
    pulse.duration1 = static_cast<uint16_t>(periodTicks - 3U);
    if (rmt_write_items(channel_, &pulse, 1, false) != ESP_OK) {
        return false;
    }
    transmitting_ = true;
    return true;
}

void Esp32RmtStepPulseEngine::stop() {
    if (initialized_ && transmitting_) {
        rmt_tx_stop(channel_);
        transmitting_ = false;
    }
}

}

#endif
