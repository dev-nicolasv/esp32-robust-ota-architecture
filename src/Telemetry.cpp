#include "Telemetry.h"

RuntimeTelemetry &GetRuntimeTelemetry() {
  static RuntimeTelemetry telemetry;
  return telemetry;
}
