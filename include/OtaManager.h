#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "AppConfig.h"

class OtaManager {
public:
  OtaManager();

  void begin();
  void process();

private:
  struct OtaMetadata {
    std::string version;
    std::string binaryUrl;
    std::string sha256;
    size_t sizeBytes = 0;
  };

  bool confirmRunningImageIfPendingRollback();
  bool ensureWifiConnected();

  bool fetchMetadata(const app::OtaSourceConfig &source, OtaMetadata &metadata);
  bool fetchJsonDocument(const app::OtaSourceConfig &source,
                         const char *url,
                         JsonDocument &doc,
                         int *httpStatus = nullptr);
  bool parseStandardMetadata(const JsonDocument &doc, OtaMetadata &metadata);
  bool parseThingsBoardMetadata(const JsonDocument &doc, OtaMetadata &metadata);

  bool downloadAndStageImage(const app::OtaSourceConfig &source, const OtaMetadata &metadata);
  bool reportThingsBoardProgress(const app::OtaSourceConfig &source,
                                 const OtaMetadata &metadata,
                                 const char *state,
                                 uint32_t progressPercent,
                                 const char *message = nullptr);

  bool isSourceConfigured(const app::OtaSourceConfig &source) const;
  bool isValidHttpsUrl(const std::string &url) const;

  int compareVersions(const std::string &lhs, const std::string &rhs) const;
  std::string currentFirmwareVersion() const;
  std::string normalizeSha256(const std::string &raw) const;

  uint32_t lastCheckMs_ = 0;
  bool initialized_ = false;
};
