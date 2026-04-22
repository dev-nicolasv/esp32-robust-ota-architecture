#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class ControlTask {
public:
  static ControlTask &instance();

  bool start();

private:
  ControlTask() = default;

  static void taskEntry(void *param);
  void run();

  void initHardware();
  float readSensorVolts() const;
  float sensorVoltsToProcessPercent(float sensorVolts) const;
  uint32_t percentToPwmDuty(float dutyPercent) const;

  TaskHandle_t taskHandle_ = nullptr;
};
