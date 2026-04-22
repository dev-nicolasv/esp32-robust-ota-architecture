# ThingsBoard OTA Deployment

## 1. Supported integration mode

Firmware polls a ThingsBoard HTTPS endpoint and parses shared attributes.

Expected keys:

- `fw_version`
- `fw_url`
- `fw_checksum`
- `fw_size`

## 2. Device configuration

Set in build flags:

- `OTA_THINGSBOARD_METADATA_URL`
- `OTA_THINGSBOARD_TELEMETRY_URL` (optional progress reporting)
- `OTA_THINGSBOARD_AUTH_HEADER_NAME` and `OTA_THINGSBOARD_AUTH_HEADER_VALUE` when required

## 3. Example metadata endpoint

Common pattern using device token:

`https://<thingsboard-host>/api/v1/<TOKEN>/attributes?sharedKeys=fw_version,fw_url,fw_checksum,fw_size`

## 4. Progress telemetry

When telemetry URL is configured, firmware can report:

- `ota_state` (`downloading`, `staged`, `failed`)
- `ota_progress`
- `ota_target_version`
- `ota_message` (optional)

## 5. Operational recommendations

- Restrict who can update shared attributes
- Enforce HTTPS and valid server certificates
- Keep firmware binaries in immutable object storage
