#!/usr/bin/env python3
"""Generate OTA metadata JSON with deterministic SHA-256 checksum."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate OTA metadata manifest")
    parser.add_argument("--firmware", required=True, type=Path, help="Path to firmware binary")
    parser.add_argument("--version", required=True, help="Semantic firmware version")
    parser.add_argument("--url", required=True, help="HTTPS URL to firmware binary")
    parser.add_argument("--output", required=True, type=Path, help="Output metadata JSON path")
    args = parser.parse_args()

    if not args.url.startswith("https://"):
        raise SystemExit("URL must be HTTPS")

    if not args.firmware.exists():
        raise SystemExit(f"Firmware does not exist: {args.firmware}")

    checksum = sha256_file(args.firmware)
    size = args.firmware.stat().st_size

    metadata = {
        "version": args.version,
        "url": args.url,
        "sha256": checksum,
        "size": size,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(f"Manifest written to {args.output}")


if __name__ == "__main__":
    main()
