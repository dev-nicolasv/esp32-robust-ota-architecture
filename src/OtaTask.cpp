#include "OtaTask.h"

#include <esp_log.h>
#include <esp_task_wdt.h>

#include "AppConfig.h"
#include "OtaManager.h"

namespace {
constexpr const char *kTag = "OtaTask";
}

OtaTask &OtaTask::instance() {
  static OtaTask task;
  return task;
}

bool OtaTask::start() {
  if (taskHandle_ != nullptr) {
    return true;
  }

  const BaseType_t rc = xTaskCreatePinnedToCore(taskEntry,
                                                 "OtaTask",
                                                 app::kOtaTaskStackWords,
                                                 this,
                                                 app::kOtaTaskPriority,
                                                 &taskHandle_,
                                                 0);

  if (rc != pdPASS) {
    ESP_LOGE(kTag, "Failed to create OTA task");
    taskHandle_ = nullptr;
    return false;
  }

  return true;
}

void OtaTask::taskEntry(void *param) {
  auto *self = static_cast<OtaTask *>(param);
  self->run();
}

void OtaTask::run() {
  if (esp_task_wdt_add(nullptr) != ESP_OK) {
    ESP_LOGW(kTag, "Unable to subscribe OTA task to task watchdog");
  }

  OtaManager manager;
  manager.begin();

  TickType_t lastWakeTick = xTaskGetTickCount();
  const TickType_t cycleTicks = pdMS_TO_TICKS(app::kOtaTaskCycleMs);

  for (;;) {
    manager.process();
    esp_task_wdt_reset();
    vTaskDelayUntil(&lastWakeTick, cycleTicks);
  }
}
