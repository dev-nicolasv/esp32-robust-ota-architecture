#include <Arduino.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>

#include "AppConfig.h"
#include "ControlTask.h"
#include "OtaTask.h"

namespace {
constexpr const char *kTag = "Main";

void InitTaskWatchdog() {
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t config = {
      .timeout_ms = app::kTaskWdtTimeoutMs,
      .idle_core_mask = (1U << portNUM_PROCESSORS) - 1U,
      .trigger_panic = true,
  };
  const esp_err_t err = esp_task_wdt_init(&config);
#else
  const esp_err_t err = esp_task_wdt_init(app::kTaskWdtTimeoutMs / 1000, true);
#endif

  if (err == ESP_OK) {
    ESP_LOGI(kTag, "Task watchdog initialized (%lu ms)",
             static_cast<unsigned long>(app::kTaskWdtTimeoutMs));
    return;
  }

  if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "Task watchdog already initialized");
    return;
  }

  ESP_LOGE(kTag, "Task watchdog initialization failed: %s", esp_err_to_name(err));
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(300);

  ESP_LOGI(kTag, "Booting firmware version %s", app::kFirmwareVersion);

  InitTaskWatchdog();

  if (esp_task_wdt_add(nullptr) != ESP_OK) {
    ESP_LOGW(kTag, "Unable to subscribe loop task to task watchdog");
  }

  if (!ControlTask::instance().start()) {
    ESP_LOGE(kTag, "Control task did not start, forcing reboot");
    delay(1000);
    esp_restart();
  }

  if (!OtaTask::instance().start()) {
    ESP_LOGE(kTag, "OTA task did not start, forcing reboot");
    delay(1000);
    esp_restart();
  }
}

void loop() {
  // Main loop remains intentionally lightweight; all real-time work runs in dedicated tasks.
  esp_task_wdt_reset();
  vTaskDelay(pdMS_TO_TICKS(1000));
}
