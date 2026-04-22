# AWS OTA Deployment

## 1. Recommended topology

- Firmware binaries stored in S3
- Metadata served through API Gateway + Lambda (or static S3 object)
- Optional authentication via API Gateway authorizer

## 2. Metadata contract

Expose JSON with fields:

- `version`
- `url` (HTTPS)
- `sha256`
- `size`

## 3. Device configuration

Set in build flags:

- `OTA_AWS_METADATA_URL`
- `OTA_AWS_AUTH_HEADER_NAME` (optional)
- `OTA_AWS_AUTH_HEADER_VALUE` (optional)

## 4. Release flow

1. Build firmware artifact (`pio run`)
2. Generate SHA-256 and metadata:

```bash
python3 tools/generate_ota_manifest.py \
  --firmware .pio/build/esp32dev/firmware.bin \
  --version 1.0.1 \
  --url https://your-bucket.s3.amazonaws.com/esp32/firmware-1.0.1.bin \
  --output release/metadata.json
```

3. Upload binary and metadata to S3
4. Invalidate cache (if CloudFront is used)

## 5. Security controls

- Use least-privilege IAM roles for CI/CD uploads
- Enable S3 object versioning and audit logging
- Protect metadata endpoint against unauthorized writes
