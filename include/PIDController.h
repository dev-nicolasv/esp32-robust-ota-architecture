#pragma once

#include <cstdint>

class PIDController {
public:
  PIDController(float kp, float ki, float kd, float outputMin, float outputMax);

  void setTunings(float kp, float ki, float kd);
  void setOutputLimits(float outputMin, float outputMax);
  void reset();

  // Deterministic single-step PID execution.
  // nowMicros is injected by the caller to avoid hidden timing side effects.
  float compute(float setpoint, float measurement, uint64_t nowMicros);

private:
  float kp_;
  float ki_;
  float kd_;

  float outputMin_;
  float outputMax_;

  float integral_;
  float previousMeasurement_;
  float previousOutput_;

  uint64_t previousTimestampMicros_;
  bool initialized_;
};
