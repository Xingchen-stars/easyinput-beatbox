#!/usr/bin/env python3
"""Static sanity checks for embedded 32 kHz mono s16le audio assets."""

from pathlib import Path
from array import array
import math
import sys

ROOT = Path(__file__).resolve().parents[1]
SAMPLE_DIR = ROOT / "firmware" / "main" / "audio" / "samples"
SAMPLE_RATE = 32_000
DRUM_SAMPLES = {
    "kick.raw",
    "snare.raw",
    "hihat_closed.raw",
    "hihat_open.raw",
    "clap.raw",
    "rim.raw",
}
TEMPO_PROMPTS = {
    "tempo_slow.raw",
    "tempo_original.raw",
    "tempo_fast.raw",
}
MODE_PROMPTS = {
    "mode_beatbox.raw",
    "mode_easyinput.raw",
}


def inspect(path: Path, duration_range: tuple[float, float]) -> tuple[float, int, float]:
    raw = path.read_bytes()
    if len(raw) < 2 or len(raw) % 2:
        raise AssertionError(f"{path.name}: invalid s16le byte count")

    samples = array("h")
    samples.frombytes(raw)
    if sys.byteorder != "little":
        samples.byteswap()

    peak = max(abs(value) for value in samples)
    rms = math.sqrt(sum(value * value for value in samples) / len(samples))
    duration_ms = len(samples) * 1000 / SAMPLE_RATE

    if peak < 4_000:
        raise AssertionError(f"{path.name}: peak too quiet ({peak})")
    if rms < 500:
        raise AssertionError(f"{path.name}: RMS too quiet ({rms:.0f})")
    minimum_ms, maximum_ms = duration_range
    if not minimum_ms <= duration_ms <= maximum_ms:
        raise AssertionError(f"{path.name}: unexpected duration ({duration_ms:.1f} ms)")
    return duration_ms, peak, rms


def main() -> None:
    found = {path.name for path in SAMPLE_DIR.glob("*.raw")}
    expected = DRUM_SAMPLES | TEMPO_PROMPTS | MODE_PROMPTS
    if found != expected:
        raise AssertionError(f"sample set mismatch: expected {expected}, got {found}")

    for name in sorted(DRUM_SAMPLES):
        duration, peak, rms = inspect(SAMPLE_DIR / name, (40, 400))
        print(f"{name:18} {duration:6.1f} ms  peak={peak:5d}  rms={rms:7.1f}")
    for name in sorted(TEMPO_PROMPTS):
        duration, peak, rms = inspect(SAMPLE_DIR / name, (350, 2000))
        print(f"{name:18} {duration:6.1f} ms  peak={peak:5d}  rms={rms:7.1f}")
    for name in sorted(MODE_PROMPTS):
        duration, peak, rms = inspect(SAMPLE_DIR / name, (350, 2500))
        print(f"{name:18} {duration:6.1f} ms  peak={peak:5d}  rms={rms:7.1f}")
    print("PASS: six drum samples, three tempo prompts, and two mode prompts are audible-range")


if __name__ == "__main__":
    main()
