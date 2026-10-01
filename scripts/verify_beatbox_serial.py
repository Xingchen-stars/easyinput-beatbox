#!/usr/bin/env python3
"""Verify a running EasyInput Beatbox over its USB serial fallback."""

from __future__ import annotations

import argparse
import json
import sys
import time

import serial


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Request and validate the Beatbox protocol v3 startup state."
    )
    parser.add_argument("--port", required=True, help="Serial port, for example COM3")
    parser.add_argument("--timeout", type=float, default=10.0, help="Seconds to wait")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    frames: list[dict[str, object]] = []

    try:
        with serial.Serial(
            args.port,
            115200,
            timeout=0.2,
            write_timeout=1.0,
        ) as device:
            # Leave the ESP32-S3 out of its automatic reset/bootloader signal state.
            device.dtr = False
            device.rts = False
            device.reset_input_buffer()
            time.sleep(0.5)
            device.write(b'{"t":"ping"}\n')
            device.flush()

            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                raw = device.readline()
                if not raw:
                    continue
                text = raw.decode("utf-8", errors="replace").strip()
                start = text.find("{")
                if start < 0:
                    continue
                try:
                    frame = json.loads(text[start:])
                except json.JSONDecodeError:
                    continue
                if isinstance(frame, dict):
                    frames.append(frame)

                kinds = {str(item.get("t", "")) for item in frames}
                banks = {
                    int(item["bank"])
                    for item in frames
                    if item.get("t") == "pattern" and isinstance(item.get("bank"), int)
                }
                if {"hello", "state", "pattern"}.issubset(kinds) and banks == {0, 1, 2}:
                    break
    except serial.SerialException as exc:
        print(f"ERROR: cannot use {args.port}: {exc}", file=sys.stderr)
        return 2

    hello = next((item for item in frames if item.get("t") == "hello"), None)
    state = next((item for item in frames if item.get("t") == "state"), None)
    banks = sorted(
        {
            int(item["bank"])
            for item in frames
            if item.get("t") == "pattern" and isinstance(item.get("bank"), int)
        }
    )

    problems: list[str] = []
    if hello is None:
        problems.append("missing hello")
    else:
        if hello.get("v") != 3:
            problems.append(f"protocol is {hello.get('v')!r}, expected 3")
        if hello.get("name") != "EasyInput Beatbox":
            problems.append("unexpected device name")
        caps = hello.get("caps")
        if not isinstance(caps, list) or "ble_direct" not in caps:
            problems.append("missing ble_direct capability")
    if state is None:
        problems.append("missing state")
    if banks != [0, 1, 2]:
        problems.append(f"pattern banks are {banks}, expected [0, 1, 2]")

    if problems:
        print("FAIL: " + "; ".join(problems))
        print(f"Parsed {len(frames)} protocol frames.")
        return 1

    assert hello is not None
    assert state is not None
    print("PASS: EasyInput Beatbox protocol v3 is running.")
    print(f"Capabilities: {', '.join(str(item) for item in hello['caps'])}")
    print(
        "State: "
        f"bpm={state.get('bpm')} run={state.get('run')} "
        f"variation={state.get('var')} mode={state.get('mode')}"
    )
    print("Pattern banks: 0, 1, 2")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
