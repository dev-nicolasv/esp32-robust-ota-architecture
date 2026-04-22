#include "PIDController.h"

#include <algorithm>

PIDController::PIDController(float kp, float ki, float kd, float outputMin, float outputMax)
    : kp_(kp), ki_(ki), kd_(kd), outputMin_(outputMin), outputMax_(outputMax), integral_(0.0f),
      previousMeasurement_(0.0f), previousOutput_(0.0f), previousTimestampMicros_(0),
      initialized_(false) {}

void PIDController::setTunings(float kp, float ki, float kd) {
  kp_ = kp;
  ki_ = ki;
  kd_ = kd;
}

void PIDController::setOutputLimits(float outputMin, float outputMax) {
  outputMin_ = outputMin;
  outputMax_ = outputMax;
  integral_ = std::clamp(integral_, outputMin_, outputMax_);
  previousOutput_ = std::clamp(previousOutput_, outputMin_, outputMax_);
}

void PIDController::reset() {
  integral_ = 0.0f;
  previousMeasurement_ = 0.0f;
  previousOutput_ = 0.0f;
  previousTimestampMicros_ = 0;
  initialized_ = false;
}

float PIDController::compute(float setpoint, float measurement, uint64_t nowMicros) {
  if (!initialized_) {
    previousMeasurement_ = measurement;
    previousTimestampMicros_ = nowMicros;
    previousOutput_ = 0.0f;
    initialized_ = true;
    return previousOutput_;
  }

  const uint64_t elapsedMicros = nowMicros - previousTimestampMicros_;
  if (elapsedMicros == 0) {
    return previousOutput_;
  }

  const float dt = static_cast<float>(elapsedMicros) / 1000000.0f;
  const float error = setpoint - measurement;

  const float proportional = kp_ * error;
  const float derivative = -kd_ * ((measurement - previousMeasurement_) / dt);

  // Conditional integration to avoid wind-up while output remains saturated.
  const float unconstrainedBeforeIntegral = proportional + integral_ + derivative;
  const float constrainedBeforeIntegral = std::clamp(unconstrainedBeforeIntegral, outputMin_, outputMax_);
  const bool outputHighAndErrorPositive =
      constrainedBeforeIntegral >= outputMax_ && error > 0.0f;
  const bool outputLowAndErrorNegative =
      constrainedBeforeIntegral <= outputMin_ && error < 0.0f;

  if (!(outputHighAndErrorPositive || outputLowAndErrorNegative)) {
    integral_ += ki_ * error * dt;
    integral_ = std::clamp(integral_, outputMin_, outputMax_);
  }

  const float unconstrained = proportional + integral_ + derivative;
  const float constrained = std::clamp(unconstrained, outputMin_, outputMax_);

  previousMeasurement_ = measurement;
  previousTimestampMicros_ = nowMicros;
  previousOutput_ = constrained;
  return constrained;
}
