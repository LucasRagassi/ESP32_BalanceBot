#include <unity.h>

#include "balance/angle_estimator.hpp"
#include "balance/command_manager.hpp"
#include "balance/config.hpp"
#include "balance/config_manager.hpp"
#include "balance/mocks.hpp"
#include "balance/motion_controller.hpp"
#include "balance/pid.hpp"
#include "balance/safety.hpp"

#include <cmath>
#include <limits>

using namespace balance;

class MemoryConfigStorage final : public ConfigStorage {
public:
    bool load(RobotConfig& value) override {
        if (!available) return false;
        value = stored;
        return true;
    }
    bool save(const RobotConfig& value) override {
        if (!writable) return false;
        stored = value;
        available = true;
        return true;
    }
    RobotConfig stored{};
    bool available = false;
    bool writable = true;
};

void test_pid_clamps_output_and_rejects_invalid_dt() {
    Pid pid(PidConfig{10.0F, 0.0F, 0.0F, -5.0F, 5.0F, -10.0F, 10.0F});
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 5.0F, pid.update(1.0F, 0.0F, 0.005F));
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, pid.update(1.0F, 0.0F, 0.0F));
}

void test_pid_integral_is_bounded() {
    Pid pid(PidConfig{0.0F, 2.0F, 0.0F, -100.0F, 100.0F, -0.5F, 0.5F});
    for (int i = 0; i < 100; ++i) {
        pid.update(1.0F, 0.0F, 1.0F);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 1.0F, pid.update(1.0F, 0.0F, 1.0F));
}

void test_configuration_validation_limits() {
    RobotConfig config;
    TEST_ASSERT_TRUE(validateConfig(config));
    config.maximumTiltDeg = 90.0F;
    TEST_ASSERT_FALSE(validateConfig(config));
    config = RobotConfig{};
    config.remoteTimeoutMs = 10;
    TEST_ASSERT_FALSE(validateConfig(config));
    config = RobotConfig{};
    config.controlFrequencyHz = 100;
    TEST_ASSERT_FALSE(validateConfig(config));
    config = RobotConfig{};
    config.leftStepper.currentLimitAmps = 2.1F;
    TEST_ASSERT_FALSE(validateConfig(config));
    config = RobotConfig{};
    config.rightStepper.maximumSpeedStepsPerSecond = 20001.0F;
    TEST_ASSERT_FALSE(validateConfig(config));
}

void test_config_manager_validates_before_persisting() {
    MemoryConfigStorage storage;
    ConfigManager manager(storage);
    RobotConfig loaded;
    TEST_ASSERT_FALSE(manager.load(loaded));
    TEST_ASSERT_TRUE(validateConfig(loaded));
    RobotConfig invalid;
    invalid.maximumTiltDeg = 80.0F;
    TEST_ASSERT_FALSE(manager.save(invalid));
    TEST_ASSERT_FALSE(storage.available);
    RobotConfig valid;
    valid.remoteTimeoutMs = 900;
    valid.leftStepper.currentLimitAmps = 0.6F;
    valid.leftStepper.maximumSpeedStepsPerSecond = 850.0F;
    valid.rightStepper.currentLimitAmps = 0.75F;
    valid.rightStepper.directionInverted = false;
    TEST_ASSERT_TRUE(manager.save(valid));
    TEST_ASSERT_TRUE(manager.load(loaded));
    TEST_ASSERT_EQUAL_UINT32(900, loaded.remoteTimeoutMs);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.6F, loaded.leftStepper.currentLimitAmps);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 850.0F,
                             loaded.leftStepper.maximumSpeedStepsPerSecond);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.75F, loaded.rightStepper.currentLimitAmps);
    TEST_ASSERT_FALSE(loaded.rightStepper.directionInverted);
}

void test_state_machine_transitions_and_sensor_fault() {
    SafetyStateMachine state;
    TEST_ASSERT_TRUE(state.beginCalibration());
    TEST_ASSERT_FALSE(state.beginCalibration());
    TEST_ASSERT_TRUE(state.finishCalibration(true));
    TEST_ASSERT_EQUAL(static_cast<int>(RobotState::Ready), static_cast<int>(state.state()));
    TEST_ASSERT_TRUE(state.requestBalance(true, 0.0F));
    state.updateBalanceHealth(false, false, 0.0F, 35.0F, true);
    TEST_ASSERT_EQUAL(static_cast<int>(RobotState::Fault), static_cast<int>(state.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(FaultCode::InvalidSensorReading),
                      static_cast<int>(state.fault()));
}

void test_state_machine_rejects_tilt_and_open_interlock() {
    SafetyStateMachine tilted;
    tilted.beginCalibration();
    tilted.finishCalibration(true);
    tilted.requestBalance(true, 0.0F);
    tilted.updateBalanceHealth(true, false, 36.0F, 35.0F, true);
    TEST_ASSERT_EQUAL(static_cast<int>(FaultCode::ExcessiveTilt),
                      static_cast<int>(tilted.fault()));

    SafetyStateMachine interlock;
    interlock.beginCalibration();
    interlock.finishCalibration(true);
    TEST_ASSERT_FALSE(interlock.requestBalance(false, 0.0F));
    TEST_ASSERT_EQUAL(static_cast<int>(FaultCode::SafetyInterlock),
                      static_cast<int>(interlock.fault()));
}

void test_state_machine_latches_control_deadline_failure() {
    SafetyStateMachine state;
    state.beginCalibration();
    state.finishCalibration(true);
    TEST_ASSERT_TRUE(state.requestBalance(true, 0.0F));
    state.updateBalanceHealth(true, true, 0.0F, 35.0F, true);
    TEST_ASSERT_EQUAL(static_cast<int>(FaultCode::ControlDeadline),
                      static_cast<int>(state.fault()));
}

void test_remote_timeout_zeros_command_and_disconnect_is_not_a_balance_fault() {
    RemoteCommandManager commands(500);
    TEST_ASSERT_TRUE(commands.update(0.6F, -0.2F, 1000));
    MotionCommand active = commands.current(1200);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.6F, active.velocity);
    MotionCommand expired = commands.current(1500);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, expired.velocity);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, expired.turn);
    TEST_ASSERT_TRUE(commands.expired());
    TEST_ASSERT_FALSE(commands.update(1.1F, 0.0F, 1600));
    commands.update(0.4F, 0.0F, 1700);
    SafetyStateMachine state;
    state.beginCalibration();
    state.finishCalibration(true);
    TEST_ASSERT_TRUE(state.requestBalance(true, 0.0F));
    commands.setConnected(false);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, commands.current(1701).velocity);
    TEST_ASSERT_EQUAL(static_cast<int>(RobotState::Balancing), static_cast<int>(state.state()));
}

void test_remote_command_respects_configured_reference_limit() {
    RemoteCommandManager commands(500, 0.4F);
    TEST_ASSERT_TRUE(commands.update(0.4F, -0.4F, 10));
    TEST_ASSERT_FALSE(commands.update(0.41F, 0.0F, 20));
    TEST_ASSERT_FALSE(commands.update(0.0F, -0.5F, 20));
}

void test_independent_stepper_profiles_and_ramped_response() {
    SimulatedStepper leftModel;
    SimulatedStepper rightModel;
    StepperAxisController left(leftModel);
    StepperAxisController right(rightModel);
    const StepperMotorConfig leftConfig{0.55F, 900.0F, 3000.0F, false};
    const StepperMotorConfig rightConfig{0.8F, 1200.0F, 4000.0F, true};
    TEST_ASSERT_TRUE(left.configure(leftConfig));
    TEST_ASSERT_TRUE(right.configure(rightConfig));
    left.setEnabled(true);
    right.setEnabled(true);
    TEST_ASSERT_TRUE(left.update(1.0F, 0.05F));
    TEST_ASSERT_TRUE(right.update(1.0F, 0.05F));
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 150.0F, left.velocity());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 200.0F, right.velocity());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, -200.0F, rightModel.velocity());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 900.0F,
                             left.config().maximumSpeedStepsPerSecond);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 1200.0F,
                             right.config().maximumSpeedStepsPerSecond);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.55F, left.config().currentLimitAmps);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.8F, right.config().currentLimitAmps);
}

void test_stepper_rejects_bad_limits_and_bounds_demand() {
    SimulatedStepper model;
    StepperAxisController axis(model);
    StepperMotorConfig invalid{0.0F, 0.0F, 100.0F, false};
    TEST_ASSERT_FALSE(axis.configure(invalid));
    StepperMotorConfig config{0.0F, 1000.0F, 500.0F, false};
    TEST_ASSERT_TRUE(axis.configure(config));
    axis.setEnabled(true);
    TEST_ASSERT_FALSE(axis.update(0.5F, 0.0F));
    TEST_ASSERT_FALSE(axis.update(NAN, 0.005F));
    TEST_ASSERT_TRUE(axis.update(10.0F, 0.01F));
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 5.0F, axis.velocity());
    for (int i = 0; i < 300; ++i) axis.update(10.0F, 0.01F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 1000.0F, axis.velocity());
    axis.setEnabled(false);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, axis.velocity());
    TEST_ASSERT_FALSE(model.enabled());
}

void test_simulated_missed_steps_trigger_fault() {
    SimulatedStepper model;
    StepperAxisController axis(model);
    TEST_ASSERT_TRUE(axis.configure(StepperMotorConfig{0.0F, 1000.0F, 10000.0F, false}));
    axis.setEnabled(true);
    TEST_ASSERT_TRUE(axis.update(1.0F, 0.1F));
    model.simulateMissedSteps(0.5F);
    model.advance(0.1F);
    MissedStepMonitor monitor(32);
    TEST_ASSERT_FALSE(monitor.update(model.commandedSteps(), model.measuredSteps()));
    TEST_ASSERT_EQUAL_INT32(50, monitor.difference());

    SafetyStateMachine state;
    state.beginCalibration();
    state.finishCalibration(true);
    TEST_ASSERT_TRUE(state.requestBalance(true, 0.0F));
    state.updateBalanceHealth(true, false, 0.0F, 35.0F, true, true);
    TEST_ASSERT_EQUAL(static_cast<int>(FaultCode::MissedSteps),
                      static_cast<int>(state.fault()));
}

void test_complementary_estimator_rejects_invalid_readings() {
    ComplementaryAngleEstimator estimator;
    ImuReading reading{1.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, true};
    float angle = 0.0F;
    TEST_ASSERT_TRUE(estimator.update(reading, 0.005F, angle));
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 45.0F, angle);
    reading.valid = false;
    TEST_ASSERT_FALSE(estimator.update(reading, 0.005F, angle));
    reading.valid = true;
    TEST_ASSERT_FALSE(estimator.update(reading, 0.0F, angle));
}

void test_outer_loop_produces_bounded_velocity_angle_reference() {
    PidOuterLoopController outer(
        PidConfig{10.0F, 0.0F, 0.0F, -8.0F, 8.0F, -4.0F, 4.0F},
        PidConfig{2.0F, 0.0F, 0.0F, -8.0F, 8.0F, -4.0F, 4.0F});
    MotionCommand command;
    command.velocity = 1.0F;
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 8.0F,
                             outer.velocityAngleReference(command, 0.0F, 0.005F));
    TEST_ASSERT_FLOAT_WITHIN(0.001F, -2.0F,
                             outer.positionAngleReference(0.0F, 1.0F, 0.005F));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_pid_clamps_output_and_rejects_invalid_dt);
    RUN_TEST(test_pid_integral_is_bounded);
    RUN_TEST(test_configuration_validation_limits);
    RUN_TEST(test_config_manager_validates_before_persisting);
    RUN_TEST(test_state_machine_transitions_and_sensor_fault);
    RUN_TEST(test_state_machine_rejects_tilt_and_open_interlock);
    RUN_TEST(test_state_machine_latches_control_deadline_failure);
    RUN_TEST(test_remote_timeout_zeros_command_and_disconnect_is_not_a_balance_fault);
    RUN_TEST(test_remote_command_respects_configured_reference_limit);
    RUN_TEST(test_independent_stepper_profiles_and_ramped_response);
    RUN_TEST(test_stepper_rejects_bad_limits_and_bounds_demand);
    RUN_TEST(test_simulated_missed_steps_trigger_fault);
    RUN_TEST(test_complementary_estimator_rejects_invalid_readings);
    RUN_TEST(test_outer_loop_produces_bounded_velocity_angle_reference);
    return UNITY_END();
}
