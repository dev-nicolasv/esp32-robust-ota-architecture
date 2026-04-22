#include "OtaManager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <sstream>
#include <vector>

#include "Telemetry.h"

namespace {
constexpr const char *kTag = "OtaManager";

bool ApplyOptionalHeader(esp_http_client_handle_t client,
                         const char *headerName,
                         const char *headerValue) {
  if (headerName == nullptr || headerValue == nullptr || headerName[0] == '\0' ||
      headerValue[0] == '\0') {
    return true;
  }

  return esp_http_client_set_header(client, headerName, headerValue) == ESP_OK;
}

esp_http_client_handle_t BuildSecureHttpClient(const app::OtaSourceConfig &source,
                                               const char *url,
                                               esp_http_client_method_t method) {
  esp_http_client_config_t config = {};
  config.url = url;
  config.method = method;
  config.timeout_ms = app::kHttpTimeoutMs;
  config.transport_type = HTTP_TRANSPORT_OVER_SSL;
  config.skip_cert_common_name_check = false;
  config.buffer_size = app::kOtaChunkBytes;
  config.buffer_size_tx = 1024;
  config.keep_alive_enable = true;

  if (source.rootCaPem != nullptr && source.rootCaPem[0] != '\0') {
    config.cert_pem = source.rootCaPem;
  } else {
    config.crt_bundle_attach = arduino_esp_crt_bundle_attach;
  }

  return esp_http_client_init(&config);
}

std::string ReadJsonString(const JsonVariantConst &value) {
  if (value.is<const char *>()) {
    return std::string(value.as<const char *>());
  }

  if (value.is<std::string>()) {
    return value.as<std::string>();
  }

  return {};
}

} // namespace

OtaManager::OtaManager() = default;

void OtaManager::begin() {
  if (initialized_) {
    return;
  }

  confirmRunningImageIfPendingRollback();
  lastCheckMs_ = millis();
  initialized_ = true;
}

void OtaManager::process() {
  if (!initialized_) {
    begin();
  }

  const uint32_t nowMs = millis();
  if ((nowMs - lastCheckMs_) < app::kOtaCheckPeriodMs) {
    return;
  }

  lastCheckMs_ = nowMs;

  if (!ensureWifiConnected()) {
    ESP_LOGW(kTag, "Skipping OTA check because Wi-Fi is unavailable");
    return;
  }

  auto &telemetry = GetRuntimeTelemetry();
  telemetry.otaInProgress.store(false);
  telemetry.otaProgressPercent.store(0);

  for (size_t sourceIndex = 0; sourceIndex < app::kOtaSourceCount; ++sourceIndex) {
    const auto &source = app::kOtaSources[sourceIndex];
    if (!isSourceConfigured(source)) {
      continue;
    }

    // Source index is exported for external monitoring/debug dashboards.
    telemetry.otaLastSourceIndex.store(static_cast<uint32_t>(sourceIndex));

    OtaMetadata metadata;
    if (!fetchMetadata(source, metadata)) {
      continue;
    }

    const std::string runningVersion = currentFirmwareVersion();
    if (compareVersions(metadata.version, runningVersion) <= 0) {
      ESP_LOGI(kTag,
               "Source %s has no newer firmware (remote=%s, local=%s)",
               source.name,
               metadata.version.c_str(),
               runningVersion.c_str());
      continue;
    }

    ESP_LOGI(kTag,
             "Update candidate from %s accepted (remote=%s, local=%s)",
             source.name,
             metadata.version.c_str(),
             runningVersion.c_str());

    telemetry.otaInProgress.store(true);
    reportThingsBoardProgress(source, metadata, "downloading", 0, "OTA download started");

    if (downloadAndStageImage(source, metadata)) {
      reportThingsBoardProgress(source, metadata, "staged", 100, "OTA image staged, rebooting");
      ESP_LOGI(kTag, "OTA image successfully staged from %s. Rebooting.", source.name);
      vTaskDelay(pdMS_TO_TICKS(1500));
      esp_restart();
      return;
    }

    telemetry.otaInProgress.store(false);
    telemetry.otaProgressPercent.store(0);
    telemetry.otaLastError.store(1);
    reportThingsBoardProgress(source, metadata, "failed", telemetry.otaProgressPercent.load(),
                              "OTA staging failed");

    ESP_LOGW(kTag, "Staging failed from source %s, trying next source if available", source.name);
  }
}

bool OtaManager::confirmRunningImageIfPendingRollback() {
  const esp_partition_t *runningPartition = esp_ota_get_running_partition();
  if (runningPartition == nullptr) {
    ESP_LOGE(kTag, "Cannot inspect running partition for rollback state");
    return false;
  }

  esp_ota_img_states_t imageState;
  if (esp_ota_get_state_partition(runningPartition, &imageState) != ESP_OK) {
    return false;
  }

  if (imageState != ESP_OTA_IMG_PENDING_VERIFY) {
    return true;
  }

  // Basic power-on self-check prior to validating the freshly booted image.
  // If this check fails, firmware is explicitly marked invalid so the bootloader
  // can roll back to the previous slot on next reboot.
  const bool heapHealthy = esp_get_free_heap_size() > 50000;
  const bool controlSignalSane = std::isfinite(GetRuntimeTelemetry().sensorVoltage.load());

  if (heapHealthy && controlSignalSane) {
    ESP_LOGI(kTag, "New firmware passed startup checks, marking image as valid");
    return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
  }

  ESP_LOGE(kTag, "New firmware failed startup checks, rolling back to previous image");
  esp_ota_mark_app_invalid_rollback_and_reboot();
  return false;
}

bool OtaManager::ensureWifiConnected() {
  if (strlen(app::kWifiSsid) == 0 || strstr(app::kWifiSsid, "REPLACE_") != nullptr) {
    ESP_LOGW(kTag, "Wi-Fi credentials not configured; OTA disabled until credentials are provided");
    return false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(app::kWifiSsid, app::kWifiPassword);

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < app::kWifiConnectTimeoutMs) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(250));
  }

  if (WiFi.status() == WL_CONNECTED) {
    ESP_LOGI(kTag, "Wi-Fi connected, IP=%s", WiFi.localIP().toString().c_str());
    return true;
  }

  ESP_LOGW(kTag, "Wi-Fi connect timeout");
  return false;
}

bool OtaManager::fetchMetadata(const app::OtaSourceConfig &source, OtaMetadata &metadata) {
  JsonDocument doc;
  int httpStatus = 0;

  if (!fetchJsonDocument(source, source.metadataUrl, doc, &httpStatus)) {
    ESP_LOGW(kTag, "Metadata fetch failed from %s", source.name);
    return false;
  }

  if (httpStatus < 200 || httpStatus >= 300) {
    ESP_LOGW(kTag, "Metadata HTTP status %d from %s", httpStatus, source.name);
    return false;
  }

  bool parsed = false;
  if (source.kind == app::OtaSourceKind::ThingsBoard) {
    parsed = parseThingsBoardMetadata(doc, metadata);
  } else {
    parsed = parseStandardMetadata(doc, metadata);
  }

  if (!parsed) {
    ESP_LOGW(kTag, "Metadata parse failed for source %s", source.name);
    return false;
  }

  if (!isValidHttpsUrl(metadata.binaryUrl)) {
    ESP_LOGW(kTag, "Source %s provided non-HTTPS firmware URL: %s", source.name,
             metadata.binaryUrl.c_str());
    return false;
  }

  metadata.sha256 = normalizeSha256(metadata.sha256);
  if (metadata.sha256.size() != 64) {
    ESP_LOGW(kTag, "Source %s provided invalid SHA-256 checksum", source.name);
    return false;
  }

  return true;
}

bool OtaManager::fetchJsonDocument(const app::OtaSourceConfig &source,
                                   const char *url,
                                   JsonDocument &doc,
                                   int *httpStatus) {
  if (url == nullptr || url[0] == '\0') {
    return false;
  }

  std::string urlString(url);
  if (!isValidHttpsUrl(urlString)) {
    return false;
  }

  esp_http_client_handle_t client = BuildSecureHttpClient(source, url, HTTP_METHOD_GET);
  if (client == nullptr) {
    ESP_LOGE(kTag, "Failed to initialize HTTP client");
    return false;
  }

  bool success = false;

  do {
    if (!ApplyOptionalHeader(client, source.authHeaderName, source.authHeaderValue)) {
      ESP_LOGE(kTag, "Failed to set auth header for metadata request");
      break;
    }

    if (esp_http_client_open(client, 0) != ESP_OK) {
      ESP_LOGW(kTag, "HTTP open failed for metadata URL %s", url);
      break;
    }

    esp_http_client_fetch_headers(client);
    if (httpStatus != nullptr) {
      *httpStatus = esp_http_client_get_status_code(client);
    }

    std::string payload;
    payload.reserve(4096);
    std::array<char, 512> buffer = {};

    while (true) {
      const int bytesRead = esp_http_client_read(client, buffer.data(), buffer.size());
      if (bytesRead < 0) {
        ESP_LOGW(kTag, "HTTP read error while fetching metadata");
        break;
      }

      if (bytesRead == 0) {
        if (esp_http_client_is_complete_data_received(client)) {
          const DeserializationError err = deserializeJson(doc, payload);
          if (err) {
            ESP_LOGW(kTag, "JSON deserialize error: %s", err.c_str());
            break;
          }

          success = true;
          break;
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }

      payload.append(buffer.data(), static_cast<size_t>(bytesRead));
      if (payload.size() > 32768) {
        ESP_LOGW(kTag, "Metadata payload too large");
        break;
      }

      esp_task_wdt_reset();
    }
  } while (false);

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return success;
}

bool OtaManager::parseStandardMetadata(const JsonDocument &doc, OtaMetadata &metadata) {
  metadata.version = ReadJsonString(doc["version"]);
  metadata.binaryUrl = ReadJsonString(doc["url"]);
  metadata.sha256 = ReadJsonString(doc["sha256"]);
  metadata.sizeBytes = doc["size"].is<size_t>() ? doc["size"].as<size_t>() : 0;

  return !metadata.version.empty() && !metadata.binaryUrl.empty() && !metadata.sha256.empty();
}

bool OtaManager::parseThingsBoardMetadata(const JsonDocument &doc, OtaMetadata &metadata) {
  JsonVariantConst root = doc.as<JsonVariantConst>();
  JsonVariantConst shared = root["shared"];

  if (shared.isNull()) {
    shared = root;
  }

  metadata.version = ReadJsonString(shared["fw_version"]);
  metadata.binaryUrl = ReadJsonString(shared["fw_url"]);
  metadata.sha256 = ReadJsonString(shared["fw_checksum"]);
  metadata.sizeBytes = shared["fw_size"].is<size_t>() ? shared["fw_size"].as<size_t>() : 0;

  // Fallback aliases frequently used in custom ThingsBoard payloads.
  if (metadata.version.empty()) {
    metadata.version = ReadJsonString(shared["version"]);
  }

  if (metadata.binaryUrl.empty()) {
    metadata.binaryUrl = ReadJsonString(shared["url"]);
  }

  if (metadata.sha256.empty()) {
    metadata.sha256 = ReadJsonString(shared["sha256"]);
  }

  return !metadata.version.empty() && !metadata.binaryUrl.empty() && !metadata.sha256.empty();
}

bool OtaManager::downloadAndStageImage(const app::OtaSourceConfig &source, const OtaMetadata &metadata) {
  auto &telemetry = GetRuntimeTelemetry();

  const esp_partition_t *updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (updatePartition == nullptr) {
    ESP_LOGE(kTag, "No OTA partition available for update");
    return false;
  }

  esp_http_client_handle_t client =
      BuildSecureHttpClient(source, metadata.binaryUrl.c_str(), HTTP_METHOD_GET);
  if (client == nullptr) {
    ESP_LOGE(kTag, "Failed to create HTTP client for binary download");
    return false;
  }

  esp_ota_handle_t otaHandle = 0;
  bool otaStarted = false;
  bool shaStarted = false;
  bool success = false;

  mbedtls_sha256_context shaContext;
  mbedtls_sha256_init(&shaContext);

  do {
    if (!ApplyOptionalHeader(client, source.authHeaderName, source.authHeaderValue)) {
      ESP_LOGE(kTag, "Failed to set auth header for binary request");
      break;
    }

    if (esp_http_client_open(client, 0) != ESP_OK) {
      ESP_LOGW(kTag, "Failed to open firmware URL");
      break;
    }

    esp_http_client_fetch_headers(client);

    const int statusCode = esp_http_client_get_status_code(client);
    if (statusCode < 200 || statusCode >= 300) {
      ESP_LOGW(kTag, "Unexpected HTTP status code while downloading firmware: %d", statusCode);
      break;
    }

    const int64_t contentLength = esp_http_client_get_content_length(client);
    if (metadata.sizeBytes > 0 && contentLength > 0 &&
        static_cast<size_t>(contentLength) != metadata.sizeBytes) {
      ESP_LOGW(kTag,
               "Metadata size (%u) does not match HTTP content length (%lld)",
               static_cast<unsigned>(metadata.sizeBytes),
               static_cast<long long>(contentLength));
      break;
    }

    if (esp_ota_begin(updatePartition, OTA_SIZE_UNKNOWN, &otaHandle) != ESP_OK) {
      ESP_LOGE(kTag, "esp_ota_begin failed");
      break;
    }
    otaStarted = true;

    if (mbedtls_sha256_starts_ret(&shaContext, 0) != 0) {
      ESP_LOGE(kTag, "Failed to initialize SHA-256 context");
      break;
    }
    shaStarted = true;

    std::array<uint8_t, app::kOtaChunkBytes> chunk = {};
    size_t totalBytesWritten = 0;
    const size_t expectedSize = metadata.sizeBytes > 0
                                    ? metadata.sizeBytes
                                    : (contentLength > 0 ? static_cast<size_t>(contentLength) : 0);

    // Stream the binary in chunks to avoid large heap usage and keep WDT fed.
    bool readError = false;
    while (true) {
      const int bytesRead = esp_http_client_read(client,
                                                 reinterpret_cast<char *>(chunk.data()),
                                                 chunk.size());
      if (bytesRead < 0) {
        ESP_LOGE(kTag, "Firmware download read error");
        readError = true;
        break;
      }

      if (bytesRead == 0) {
        if (esp_http_client_is_complete_data_received(client)) {
          break;
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }

      if (esp_ota_write(otaHandle, chunk.data(), static_cast<size_t>(bytesRead)) != ESP_OK) {
        ESP_LOGE(kTag, "esp_ota_write failed");
        readError = true;
        break;
      }

      if (mbedtls_sha256_update_ret(&shaContext, chunk.data(), static_cast<size_t>(bytesRead)) != 0) {
        ESP_LOGE(kTag, "SHA-256 update failed");
        readError = true;
        break;
      }

      totalBytesWritten += static_cast<size_t>(bytesRead);
      if (expectedSize > 0) {
        const uint32_t progress =
            static_cast<uint32_t>((static_cast<uint64_t>(totalBytesWritten) * 100ULL) / expectedSize);
        const uint32_t boundedProgress = std::min<uint32_t>(progress, 100);
        telemetry.otaProgressPercent.store(boundedProgress);
        reportThingsBoardProgress(source, metadata, "downloading", boundedProgress, nullptr);
      }

      esp_task_wdt_reset();
    }

    if (readError) {
      break;
    }

    // Verify byte count when source provides file size.
    if (expectedSize > 0 && totalBytesWritten != expectedSize) {
      ESP_LOGE(kTag,
               "Downloaded firmware size mismatch (expected=%u, actual=%u)",
               static_cast<unsigned>(expectedSize),
               static_cast<unsigned>(totalBytesWritten));
      break;
    }

    std::array<uint8_t, 32> digest = {};
    if (mbedtls_sha256_finish_ret(&shaContext, digest.data()) != 0) {
      ESP_LOGE(kTag, "SHA-256 finalize failed");
      break;
    }

    std::ostringstream digestStream;
    digestStream << std::hex << std::nouppercase;
    for (uint8_t byte : digest) {
      digestStream.width(2);
      digestStream.fill('0');
      digestStream << static_cast<int>(byte);
    }

    const std::string computedSha = normalizeSha256(digestStream.str());
    const std::string expectedSha = normalizeSha256(metadata.sha256);
    // Cryptographic integrity gate: image is rejected unless digest matches.
    if (computedSha != expectedSha) {
      ESP_LOGE(kTag,
               "SHA-256 mismatch. expected=%s computed=%s",
               expectedSha.c_str(),
               computedSha.c_str());
      break;
    }

    if (esp_ota_end(otaHandle) != ESP_OK) {
      ESP_LOGE(kTag, "esp_ota_end failed");
      break;
    }
    otaStarted = false;

    // Only after a full write + hash check do we switch boot target.
    if (esp_ota_set_boot_partition(updatePartition) != ESP_OK) {
      ESP_LOGE(kTag, "esp_ota_set_boot_partition failed");
      break;
    }

    telemetry.otaProgressPercent.store(100);
    success = true;
  } while (false);

  if (shaStarted) {
    mbedtls_sha256_free(&shaContext);
  }

  if (otaStarted) {
    esp_ota_abort(otaHandle);
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return success;
}

bool OtaManager::reportThingsBoardProgress(const app::OtaSourceConfig &source,
                                           const OtaMetadata &metadata,
                                           const char *state,
                                           uint32_t progressPercent,
                                           const char *message) {
  if (source.kind != app::OtaSourceKind::ThingsBoard || source.telemetryUrl == nullptr ||
      source.telemetryUrl[0] == '\0') {
    return true;
  }

  if (!isValidHttpsUrl(source.telemetryUrl)) {
    return false;
  }

  esp_http_client_handle_t client =
      BuildSecureHttpClient(source, source.telemetryUrl, HTTP_METHOD_POST);
  if (client == nullptr) {
    return false;
  }

  bool ok = false;

  do {
    if (!ApplyOptionalHeader(client, source.authHeaderName, source.authHeaderValue)) {
      break;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");

    JsonDocument payload;
    payload["ota_state"] = state;
    payload["ota_progress"] = progressPercent;
    payload["ota_target_version"] = metadata.version;
    if (message != nullptr) {
      payload["ota_message"] = message;
    }

    std::string body;
    serializeJson(payload, body);
    esp_http_client_set_post_field(client, body.c_str(), static_cast<int>(body.size()));

    const esp_err_t rc = esp_http_client_perform(client);
    if (rc != ESP_OK) {
      break;
    }

    const int status = esp_http_client_get_status_code(client);
    ok = (status >= 200 && status < 300);
  } while (false);

  esp_http_client_cleanup(client);
  return ok;
}

bool OtaManager::isSourceConfigured(const app::OtaSourceConfig &source) const {
  if (!source.enabled || source.metadataUrl == nullptr || source.metadataUrl[0] == '\0') {
    return false;
  }

  const std::string metadata(source.metadataUrl);
  return metadata.find("replace-") == std::string::npos &&
         metadata.find("REPLACE_") == std::string::npos;
}

bool OtaManager::isValidHttpsUrl(const std::string &url) const {
  return url.rfind("https://", 0) == 0;
}

int OtaManager::compareVersions(const std::string &lhs, const std::string &rhs) const {
  auto parse = [](const std::string &value) {
    std::vector<int> parts;
    std::stringstream stream(value);
    std::string token;

    while (std::getline(stream, token, '.')) {
      size_t index = 0;
      while (index < token.size() && !std::isdigit(static_cast<unsigned char>(token[index]))) {
        ++index;
      }

      int number = 0;
      while (index < token.size() && std::isdigit(static_cast<unsigned char>(token[index]))) {
        number = (number * 10) + (token[index] - '0');
        ++index;
      }

      parts.push_back(number);
    }

    return parts;
  };

  const std::vector<int> leftParts = parse(lhs);
  const std::vector<int> rightParts = parse(rhs);
  const size_t maxParts = std::max(leftParts.size(), rightParts.size());

  for (size_t i = 0; i < maxParts; ++i) {
    const int left = i < leftParts.size() ? leftParts[i] : 0;
    const int right = i < rightParts.size() ? rightParts[i] : 0;

    if (left > right) {
      return 1;
    }

    if (left < right) {
      return -1;
    }
  }

  return 0;
}

std::string OtaManager::currentFirmwareVersion() const {
  return std::string(app::kFirmwareVersion);
}

std::string OtaManager::normalizeSha256(const std::string &raw) const {
  std::string normalized;
  normalized.reserve(raw.size());

  for (const char ch : raw) {
    if (std::isxdigit(static_cast<unsigned char>(ch))) {
      normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
  }

  return normalized;
}
