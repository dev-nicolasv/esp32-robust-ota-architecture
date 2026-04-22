# Operations Runbook

## 1. Observability points

Monitor serial logs for:

- Control loop overrun warnings
- Wi-Fi connectivity and OTA source selection
- Metadata validation failures
- SHA mismatch and OTA staging results
- Rollback events during startup validation

## 2. Normal release procedure

1. Build and test firmware in staging hardware
2. Generate and validate OTA metadata checksum
3. Publish firmware binary and metadata
4. Roll out gradually (canary subset)
5. Confirm no rollback events before full rollout

## 3. Incident response

### Symptom: Device repeatedly reboots after OTA

- Check for `PENDING_VERIFY` rollback messages
- Validate startup dependencies (power, peripherals, config)
- Revert metadata to last known-good firmware version

### Symptom: OTA download fails

- Validate TLS certificate chain
- Confirm endpoint accessibility from plant network
- Verify metadata hash and file size
- Confirm auth headers/tokens are valid

### Symptom: Control jitter increase

- Review `controlOverruns` telemetry
- Verify OTA task did not migrate into control core
- Re-check CPU load and stack high-water marks

## 4. Rollback behavior

- New image boots in pending verification state
- Startup self-check marks image valid on success
- Failed self-check triggers rollback and reboot automatically
