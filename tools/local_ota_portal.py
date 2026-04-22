#!/usr/bin/env python3
"""Minimal local HTTPS OTA portal for industrial network testing."""

from __future__ import annotations

import argparse
import json
import ssl
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path


class OtaPortalHandler(BaseHTTPRequestHandler):
    metadata_path: Path
    firmware_path: Path

    def _send_json(self, payload: dict, status: int = HTTPStatus.OK) -> None:
        body = json.dumps(payload, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_binary(self, data: bytes, status: int = HTTPStatus.OK) -> None:
        self.send_response(status)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self) -> None:
        if self.path == "/metadata.json":
            payload = json.loads(self.metadata_path.read_text(encoding="utf-8"))
            return self._send_json(payload)

        if self.path == "/firmware.bin":
            return self._send_binary(self.firmware_path.read_bytes())

        self._send_json({"error": "not_found"}, status=HTTPStatus.NOT_FOUND)


def main() -> None:
    parser = argparse.ArgumentParser(description="Run local HTTPS OTA portal")
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8443)
    parser.add_argument("--cert", required=True, type=Path)
    parser.add_argument("--key", required=True, type=Path)
    parser.add_argument("--metadata", required=True, type=Path)
    parser.add_argument("--firmware", required=True, type=Path)
    args = parser.parse_args()

    if not args.metadata.exists():
        raise SystemExit(f"Metadata not found: {args.metadata}")
    if not args.firmware.exists():
        raise SystemExit(f"Firmware not found: {args.firmware}")

    OtaPortalHandler.metadata_path = args.metadata
    OtaPortalHandler.firmware_path = args.firmware

    server = HTTPServer((args.bind, args.port), OtaPortalHandler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certfile=args.cert, keyfile=args.key)
    server.socket = context.wrap_socket(server.socket, server_side=True)

    print(f"Local OTA portal listening on https://{args.bind}:{args.port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
