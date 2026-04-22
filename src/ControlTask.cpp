#include "ControlTask.h"

#include <Arduino.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>

#include "AppConfig.h"
#include "PIDController.h"
#include "Telemetry.h"

namespace {
constexpr const char *kTag = "ControlTask";
}

ControlTask &ControlTask::instance() {
  static ControlTask task;
  return task;
}

bool ControlTask::start() {
  if (taskHandle_ != nullptr) {
    return true;
  }

  const BaseType_t rc = xTaskCreatePinnedToCore(taskEntry,
                                                 "ControlTask",
                                                 app::kControlTaskStackWords,
                                                 this,
                                                 app::kControlTaskPriority,
                                                 &taskHandle_,
                                                 1);

  if (rc != pdPASS) {
    ESP_LOGE(kTag, "Failed to create control task");
    taskHandle_ = nullptr;
    return false;
  }

  return true;
}

void ControlTask::taskEntry(void *param) {
  auto *self = static_cast<ControlTask *>(param);
  self->run();
}

void ControlTask::run() {
  initHardware();

  if (esp_task_wdt_add(nullptr) != ESP_OK) {
    ESP_LOGW(kTag, "Unable to subscribe control task to task watchdog");
  }

  // PID object lives in task context only; no shared mutable PID state across tasks.
  PIDController pid(app::kPidKp, app::kPidKi, app::kPidKd, app::kProcessMin, app::kProcessMax);

  auto &telemetry = GetRuntimeTelemetry();
  telemetry.setpointPercent.store(app::kDefaultSetpoint);

  const TickType_t periodTicks = pdMS_TO_TICKS(app::kControlPeriodMs);
  TickType_t lastWakeTick = xTaskGetTickCount();

  for (;;) {
    // Capture cycle start time for deterministic scheduling and overrun diagnostics.
    const int64_t cycleStartMicros = esp_timer_get_time();

    // 1) Acquire process feedback.
    const float sensorVolts = readSensorVolts();
    const float processPercent = sensorVoltsToProcessPercent(sensorVolts);
    const float setpointPercent = telemetry.setpointPercent.load();

    // 2) Execute PID compute at fixed cadence.
    const float outputPercent = pid.compute(setpointPercent,
                                            processPercent,
                                            static_cast<uint64_t>(cycleStartMicros));

    // 3) Apply bounded control output to heater PWM.
    ledcWrite(app::kHeaterPwmChannel, percentToPwmDuty(outputPercent));

    // 4) Publish latest values for diagnostics/remote telemetry tasks.
    telemetry.sensorVoltage.store(sensorVolts);
    telemetry.processValuePercent.store(processPercent);
    telemetry.pwmOutputPercent.store(outputPercent);

    const int64_t elapsedMicros = esp_timer_get_time() - cycleStartMicros;
    if (elapsedMicros > static_cast<int64_t>(app::kControlPeriodMs) * 1000) {
      telemetry.controlOverruns.fetch_add(1);
      ESP_LOGW(kTag, "Control loop overrun: %lld us", elapsedMicros);
    }

    // Feed WDT and sleep exactly until next period boundary.
    esp_task_wdt_reset();
    vTaskDelayUntil(&lastWakeTick, periodTicks);
  }
}

void ControlTask::initHardware() {
  analogReadResolution(12);
  analogSetPinAttenuation(app::kSensorAdcPin, ADC_11db);

  ledcSetup(app::kHeaterPwmChannel, app::kHeaterPwmFrequencyHz, app::kHeaterPwmResolutionBits);
  ledcAttachPin(app::kHeaterPwmPin, app::kHeaterPwmChannel);
  ledcWrite(app::kHeaterPwmChannel, 0);

  ESP_LOGI(kTag,
           "Hardware initialized (ADC pin=%u, PWM pin=%u, control period=%lu ms)",
           app::kSensorAdcPin,
           app::kHeaterPwmPin,
           static_cast<unsigned long>(app::kControlPeriodMs));
}

float ControlTask::readSensorVolts() const {
  // The ADC pin receives a scaled representation of the 0-10V input.
  // We sample multiple times and average to improve measurement stability
  // without introducing unpredictable blocking behavior.
  constexpr uint8_t kSamples = 4;

  uint32_t milliVoltsSum = 0;
  for (uint8_t i = 0; i < kSamples; ++i) {
    milliVoltsSum += analogReadMilliVolts(app::kSensorAdcPin);
  }

  const float adcVolts = (static_cast<float>(milliVoltsSum) / static_cast<float>(kSamples)) / 1000.0f;
  const float sensorVolts = adcVolts * app::kSensorDividerScale;

  return std::clamp(sensorVolts, 0.0f, app::kSensorElectricalMaxVolts);
}

float ControlTask::sensorVoltsToProcessPercent(float sensorVolts) const {
  const float normalized = sensorVolts / app::kSensorElectricalMaxVolts;
  return std::clamp(normalized * 100.0f, app::kProcessMin, app::kProcessMax);
}

uint32_t ControlTask::percentToPwmDuty(float dutyPercent) const {
  const float boundedPercent = std::clamp(dutyPercent, 0.0f, 100.0f);
  const uint32_t maxDuty = (1u << app::kHeaterPwmResolutionBits) - 1u;
  return static_cast<uint32_t>(std::lround((boundedPercent / 100.0f) * static_cast<float>(maxDuty)));
}
