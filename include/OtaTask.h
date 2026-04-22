#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class OtaTask {
public:
  static OtaTask &instance();

  bool start();

private:
  OtaTask() = default;

  static void taskEntry(void *param);
  void run();

  TaskHandle_t taskHandle_ = nullptr;
};
