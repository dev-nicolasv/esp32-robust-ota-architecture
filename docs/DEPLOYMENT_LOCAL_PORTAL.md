# Local Portal OTA Deployment

## 1. Use case

Recommended for plants with on-premises infrastructure and strict network segmentation.

## 2. Required assets

- HTTPS endpoint reachable by ESP32 devices
- Firmware binary (`firmware.bin`)
- Metadata document (`metadata.json`) following generic schema
- Root CA certificate trusted by device

## 3. Configure firmware

Update `platformio.ini` or CI build flags:

- `OTA_LOCAL_METADATA_URL`
- `OTA_LOCAL_AUTH_HEADER_NAME` (optional)
- `OTA_LOCAL_AUTH_HEADER_VALUE` (optional)

If local PKI is private:

- Create `include/PrivateCertificates.h`
- Set `kLocalPortalRootCaPem`

## 4. Local portal reference implementation

A minimal HTTPS portal is included:

```bash
python3 tools/local_ota_portal.py \
  --bind 0.0.0.0 \
  --port 8443 \
  --cert certs/server.crt \
  --key certs/server.key \
  --metadata release/metadata.json \
  --firmware .pio/build/esp32dev/firmware.bin
```

## 5. Validation

1. Device reads metadata from local portal URL
2. Device downloads firmware binary
3. Device verifies SHA-256
4. Device stages image and reboots
5. Device confirms image as valid after startup checks
