#!/usr/bin/env python3
"""Capture only Beatbox BLE diagnostics from the ESP32-S3 USB console."""

from __future__ import annotations

import argparse
import sys
import time

import serial


INTERESTING = (
    "beatbox_ble",
    '"t":"ble_diag"',
    "pairing window",
    "S7 physical pairing",
    "NimBLE",
)

PROTOCOL_EVENTS = ('"t":"key"', '"t":"state"', '"t":"hello"', '"t":"pattern"')


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture a bounded, filtered Beatbox BLE diagnostic log."
    )
    parser.add_argument("--port", required=True, help="Serial port, for example COM3")
    parser.add_argument("--timeout", type=float, default=60.0, help="Capture duration in seconds")
    parser.add_argument("--protocol", action="store_true", help="Also capture key/state/startup protocol frames")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    matched = 0
    try:
        with serial.Serial(args.port, 115200, timeout=0.2) as device:
            device.dtr = False
            device.rts = False
            device.reset_input_buffer()
            print(f"LISTENING={args.port} TIMEOUT={args.timeout:g}s", flush=True)
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                raw = device.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                tokens = INTERESTING + PROTOCOL_EVENTS if args.protocol else INTERESTING
                if any(token.lower() in line.lower() for token in tokens):
                    matched += 1
                    print(line, flush=True)
    except serial.SerialException as exc:
        print(f"ERROR: cannot use {args.port}: {exc}", file=sys.stderr)
        return 2

    print(f"CAPTURE_COMPLETE=yes MATCHED_LINES={matched}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
