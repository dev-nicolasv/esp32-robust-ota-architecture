# Architecture Design

## 1. Objectives

- Guarantee deterministic heater control independent of network behavior
- Enable secure OTA updates without interrupting control timing
- Provide safe firmware activation with rollback on failure

## 2. Runtime model

### Control Task (high priority, core 1)

- Periodic execution using `vTaskDelayUntil`
- Reads scaled ADC value from the 0-10V process sensor
- Runs PID compute step
- Updates PWM duty cycle for heater output
- Feeds task watchdog each cycle
- Tracks overruns for observability

### OTA Task (lower priority, core 0)

- Periodically checks update metadata from configured sources
- Downloads firmware in chunks over HTTPS
- Writes into inactive OTA partition
- Computes SHA-256 on the fly and compares with expected digest
- Switches boot partition only after full integrity validation
- Reboots into new image and relies on rollback-safe boot validation

## 3. Concurrency and isolation

- Control loop has no network dependency and never performs OTA I/O
- OTA and control tasks are pinned to separate cores
- Shared telemetry uses lock-free atomics for low contention

## 4. Safety controls

- Task watchdog subscription per critical task
- OTA transport requires TLS
- OTA payload requires SHA-256 equality before activation
- `ESP_OTA_IMG_PENDING_VERIFY` is explicitly handled at startup
- Invalid startup health triggers rollback and reboot

## 5. Source strategy

OTA source priority order:

1. Local Portal
2. AWS endpoint
3. ThingsBoard

A source is skipped when:

- Endpoint is not configured
- Endpoint is not HTTPS
- Metadata is invalid
- Candidate version is not newer
- Staging or checksum validation fails

## 6. Partition layout

`partitions.csv` defines:

- `factory`
- `ota_0`
- `ota_1`
- `otadata`

This enables A/B update strategy with rollback support.
