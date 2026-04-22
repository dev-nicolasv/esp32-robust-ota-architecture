#pragma once

#include <cstddef>
#include <cstdint>

#ifndef APP_FIRMWARE_VERSION
#define APP_FIRMWARE_VERSION "0.0.0"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef OTA_LOCAL_METADATA_URL
#define OTA_LOCAL_METADATA_URL ""
#endif

#ifndef OTA_AWS_METADATA_URL
#define OTA_AWS_METADATA_URL ""
#endif

#ifndef OTA_THINGSBOARD_METADATA_URL
#define OTA_THINGSBOARD_METADATA_URL ""
#endif

#ifndef OTA_THINGSBOARD_TELEMETRY_URL
#define OTA_THINGSBOARD_TELEMETRY_URL ""
#endif

#ifndef OTA_LOCAL_AUTH_HEADER_NAME
#define OTA_LOCAL_AUTH_HEADER_NAME ""
#endif

#ifndef OTA_LOCAL_AUTH_HEADER_VALUE
#define OTA_LOCAL_AUTH_HEADER_VALUE ""
#endif

#ifndef OTA_AWS_AUTH_HEADER_NAME
#define OTA_AWS_AUTH_HEADER_NAME ""
#endif

#ifndef OTA_AWS_AUTH_HEADER_VALUE
#define OTA_AWS_AUTH_HEADER_VALUE ""
#endif

#ifndef OTA_THINGSBOARD_AUTH_HEADER_NAME
#define OTA_THINGSBOARD_AUTH_HEADER_NAME ""
#endif

#ifndef OTA_THINGSBOARD_AUTH_HEADER_VALUE
#define OTA_THINGSBOARD_AUTH_HEADER_VALUE ""
#endif

namespace app::certs {
#if __has_include("PrivateCertificates.h")
#include "PrivateCertificates.h"
#else
inline constexpr const char *kLocalPortalRootCaPem = nullptr;
inline constexpr const char *kAwsRootCaPem = nullptr;
inline constexpr const char *kThingsBoardRootCaPem = nullptr;
#endif
} // namespace app::certs

namespace app {

inline constexpr const char *kFirmwareVersion = APP_FIRMWARE_VERSION;

inline constexpr const char *kWifiSsid = WIFI_SSID;
inline constexpr const char *kWifiPassword = WIFI_PASSWORD;

inline constexpr uint32_t kTaskWdtTimeoutMs = 10000;

inline constexpr uint8_t kSensorAdcPin = 34;
inline constexpr uint8_t kHeaterPwmPin = 25;
inline constexpr uint8_t kHeaterPwmChannel = 0;
inline constexpr uint32_t kHeaterPwmFrequencyHz = 2000;
inline constexpr uint8_t kHeaterPwmResolutionBits = 10;

inline constexpr float kSensorElectricalMaxVolts = 10.0f;
inline constexpr float kAdcElectricalMaxVolts = 3.3f;
inline constexpr float kSensorDividerScale = kSensorElectricalMaxVolts / kAdcElectricalMaxVolts;

inline constexpr float kProcessMin = 0.0f;
inline constexpr float kProcessMax = 100.0f;
inline constexpr float kDefaultSetpoint = 65.0f;

inline constexpr float kPidKp = 2.4f;
inline constexpr float kPidKi = 0.35f;
inline constexpr float kPidKd = 0.08f;

inline constexpr uint32_t kControlPeriodMs = 100;
inline constexpr uint8_t kControlTaskPriority = 10;
inline constexpr uint32_t kControlTaskStackWords = 4096;

inline constexpr uint8_t kOtaTaskPriority = 6;
inline constexpr uint32_t kOtaTaskStackWords = 16384;
inline constexpr uint32_t kOtaTaskCycleMs = 1000;
inline constexpr uint32_t kOtaCheckPeriodMs = 300000;

inline constexpr uint32_t kWifiConnectTimeoutMs = 15000;
inline constexpr uint32_t kHttpTimeoutMs = 10000;
inline constexpr size_t kOtaChunkBytes = 4096;

enum class OtaSourceKind : uint8_t {
  LocalPortal = 0,
  Aws = 1,
  ThingsBoard = 2,
};

struct OtaSourceConfig {
  OtaSourceKind kind;
  const char *name;
  bool enabled;
  const char *metadataUrl;
  const char *telemetryUrl;
  const char *authHeaderName;
  const char *authHeaderValue;
  const char *rootCaPem;
};

inline constexpr OtaSourceConfig kOtaSources[] = {
    {
        OtaSourceKind::LocalPortal,
        "local-portal",
        true,
        OTA_LOCAL_METADATA_URL,
        "",
        OTA_LOCAL_AUTH_HEADER_NAME,
        OTA_LOCAL_AUTH_HEADER_VALUE,
        certs::kLocalPortalRootCaPem,
    },
    {
        OtaSourceKind::Aws,
        "aws",
        true,
        OTA_AWS_METADATA_URL,
        "",
        OTA_AWS_AUTH_HEADER_NAME,
        OTA_AWS_AUTH_HEADER_VALUE,
        certs::kAwsRootCaPem,
    },
    {
        OtaSourceKind::ThingsBoard,
        "thingsboard",
        true,
        OTA_THINGSBOARD_METADATA_URL,
        OTA_THINGSBOARD_TELEMETRY_URL,
        OTA_THINGSBOARD_AUTH_HEADER_NAME,
        OTA_THINGSBOARD_AUTH_HEADER_VALUE,
        certs::kThingsBoardRootCaPem,
    },
};

inline constexpr size_t kOtaSourceCount = sizeof(kOtaSources) / sizeof(kOtaSources[0]);

} // namespace app
