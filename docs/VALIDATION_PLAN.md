# Validation Plan

## 1. Real-time control validation

- Verify control loop period remains within `kControlPeriodMs`
- Run 24h test and confirm no overrun growth under nominal load
- Confirm PWM output saturates correctly at 0% and 100%

## 2. OTA integrity validation

- Positive test: valid SHA-256 update from each source
- Negative test: tampered firmware (checksum mismatch)
- Negative test: metadata with invalid URL scheme (`http://`)
- Negative test: outdated version in metadata (must be ignored)

## 3. Rollback validation

- Deploy intentionally failing image
- Confirm startup self-check fails
- Confirm automatic rollback to previous partition

## 4. Source fallback validation

- Disable local portal endpoint -> ensure AWS source is used
- Disable AWS endpoint -> ensure ThingsBoard source is used
- Restore all endpoints and confirm source priority order

## 5. Watchdog validation

- Simulate task stall and verify watchdog reaction
- Ensure normal operation does not trigger watchdog resets
