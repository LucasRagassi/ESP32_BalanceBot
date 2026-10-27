#pragma once

namespace balance {

struct PidConfig {
    float kp = 0.0F;
    float ki = 0.0F;
    float kd = 0.0F;
    float outputMin = -255.0F;
    float outputMax = 255.0F;
    float integralMin = -100.0F;
    float integralMax = 100.0F;
};

class Pid {
public:
    explicit Pid(PidConfig config = {});
    float update(float setpoint, float measurement, float dtSeconds);
    void reset();
    bool setConfig(const PidConfig& config);

private:
    PidConfig config_;
    float integral_ = 0.0F;
    float previousError_ = 0.0F;
    bool hasPrevious_ = false;
};

}
