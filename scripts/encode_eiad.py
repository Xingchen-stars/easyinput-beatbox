#!/usr/bin/env python3
"""Encode mono PCM16LE into EasyInput's framed EIAD v1 IMA-ADPCM format."""

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path


STEP_TABLE = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767,
)
INDEX_DELTA = (-1, -1, -1, -1, 2, 4, 6, 8)
OUTPUT_RATE = 48_000
FRAME_SAMPLES = 480


def clamp(value: int, low: int, high: int) -> int:
    return low if value < low else high if value > high else value


def read_pcm16le(path: Path) -> list[int]:
    data = path.read_bytes()
    if not data or len(data) % 2:
        raise ValueError(f"{path} is not non-empty PCM16LE")
    return list(struct.unpack(f"<{len(data) // 2}h", data))


def resample_linear(samples: list[int], input_rate: int) -> list[int]:
    if input_rate <= 0:
        raise ValueError("input rate must be positive")
    if input_rate == OUTPUT_RATE:
        return samples.copy()
    output_count = max(1, math.ceil(len(samples) * OUTPUT_RATE / input_rate))
    output: list[int] = []
    for output_index in range(output_count):
        numerator = output_index * input_rate
        left = numerator // OUTPUT_RATE
        fraction = numerator % OUTPUT_RATE
        if left >= len(samples) - 1:
            output.append(samples[-1])
            continue
        interpolated = (
            samples[left] * (OUTPUT_RATE - fraction)
            + samples[left + 1] * fraction
        )
        output.append(clamp(round(interpolated / OUTPUT_RATE), -32768, 32767))
    return output


def encode_code(sample: int, predictor: int, step_index: int) -> tuple[int, int, int]:
    step = STEP_TABLE[step_index]
    difference = sample - predictor
    sign = 8 if difference < 0 else 0
    magnitude = -difference if difference < 0 else difference
    delta = 0
    predicted_difference = step >> 3
    if magnitude >= step:
        delta |= 4
        magnitude -= step
        predicted_difference += step
    half = step >> 1
    if magnitude >= half:
        delta |= 2
        magnitude -= half
        predicted_difference += half
    quarter = step >> 2
    if magnitude >= quarter:
        delta |= 1
        predicted_difference += quarter
    predictor += -predicted_difference if sign else predicted_difference
    predictor = clamp(predictor, -32768, 32767)
    step_index = clamp(step_index + INDEX_DELTA[delta], 0, len(STEP_TABLE) - 1)
    return sign | delta, predictor, step_index


def encode_frame(samples: list[int]) -> bytes:
    if not samples or len(samples) > FRAME_SAMPLES:
        raise ValueError("invalid EIAD frame length")
    predictor = samples[0]
    best_error: int | None = None
    best_index = 0
    best_codes: list[int] = []
    for initial_index in range(len(STEP_TABLE)):
        trial_predictor = predictor
        trial_index = initial_index
        trial_codes: list[int] = []
        squared_error = 0
        for sample in samples[1:]:
            code, trial_predictor, trial_index = encode_code(
                sample, trial_predictor, trial_index
            )
            trial_codes.append(code)
            error = sample - trial_predictor
            squared_error += error * error
        if best_error is None or squared_error < best_error:
            best_error = squared_error
            best_index = initial_index
            best_codes = trial_codes

    payload = bytearray(len(samples) // 2)
    for code_index, code in enumerate(best_codes):
        byte_index = code_index // 2
        if code_index % 2:
            payload[byte_index] |= code << 4
        else:
            payload[byte_index] = code
    return struct.pack("<HhBB", len(samples), predictor, best_index, 0) + payload


def encode_eiad(samples: list[int]) -> bytes:
    frame_count = math.ceil(len(samples) / FRAME_SAMPLES)
    if frame_count > 0xFFFF or len(samples) > 384_000:
        raise ValueError("EIAD prompt exceeds the firmware's Boot asset limits")
    header = struct.pack(
        "<4sBBIHHIH",
        b"EIAD",
        1,
        1,
        OUTPUT_RATE,
        FRAME_SAMPLES,
        frame_count,
        len(samples),
        20,
    )
    frames = bytearray()
    for start in range(0, len(samples), FRAME_SAMPLES):
        frames.extend(encode_frame(samples[start : start + FRAME_SAMPLES]))
    return header + frames


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="mono PCM16LE input")
    parser.add_argument("output", type=Path, help="EIAD v1 output")
    parser.add_argument("--input-rate", type=int, default=32_000)
    args = parser.parse_args()

    source = read_pcm16le(args.input)
    samples = resample_linear(source, args.input_rate)
    encoded = encode_eiad(samples)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(encoded)
    duration_ms = len(samples) * 1000 / OUTPUT_RATE
    print(
        f"{args.output}: {len(samples)} samples, {duration_ms:.0f} ms, "
        f"{len(encoded)} bytes"
    )


if __name__ == "__main__":
    main()
