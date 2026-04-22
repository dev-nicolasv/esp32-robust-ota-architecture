#pragma once

#include <atomic>
#include <cstdint>

struct RuntimeTelemetry {
  std::atomic<float> processValuePercent{0.0f};
  std::atomic<float> setpointPercent{0.0f};
  std::atomic<float> sensorVoltage{0.0f};
  std::atomic<float> pwmOutputPercent{0.0f};
  std::atomic<uint32_t> controlOverruns{0};

  std::atomic<bool> otaInProgress{false};
  std::atomic<uint32_t> otaProgressPercent{0};
  std::atomic<uint32_t> otaLastError{0};
  std::atomic<uint32_t> otaLastSourceIndex{0};
};

RuntimeTelemetry &GetRuntimeTelemetry();
