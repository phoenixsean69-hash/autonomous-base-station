#!/usr/bin/env python3
"""Replay a captured real 24-frame ESP32 window into the Pico Wokwi node."""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import serial


ROOT = Path(
    __file__
).resolve().parents[1]

PICO_URL = "rfc2217://localhost:4000"
PREFIX = "PICO_RESULT|"

DEFAULT_INPUT = (
    ROOT /
    "bridge" /
    "runtime" /
    "pico_temporal_window.jsonl"
)


def send_chunked(
    port,
    payload: bytes,
    chunk_size: int,
    delay_seconds: float,
) -> None:
    for offset in range(
        0,
        len(
            payload
        ),
        chunk_size,
    ):
        port.write(
            payload[
                offset:
                offset + chunk_size
            ]
        )

        port.flush()

        time.sleep(
            delay_seconds
        )


def wait_result(
    port,
    timeout: float = 8.0,
):
    deadline = (
        time.time() +
        timeout
    )

    while time.time() < deadline:
        raw = port.readline()

        if not raw:
            continue

        line = raw.decode(
            "utf-8",
            errors="replace",
        ).strip()

        if not line:
            continue

        if line.startswith(
            PREFIX
        ):
            try:
                return json.loads(
                    line[
                        len(
                            PREFIX
                        ):
                    ]
                )
            except json.JSONDecodeError:
                print(
                    "[WARN] malformed PICO_RESULT JSON"
                )

    return None


def main() -> int:
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--url",
        default=PICO_URL,
    )

    parser.add_argument(
        "--input",
        type=Path,
        default=DEFAULT_INPUT,
    )

    parser.add_argument(
        "--chunk-size",
        type=int,
        default=32,
    )

    parser.add_argument(
        "--chunk-delay",
        type=float,
        default=0.020,
    )

    args = parser.parse_args()

    if not args.input.exists():
        raise FileNotFoundError(
            f"Captured window not found: {args.input}"
        )

    lines = [
        line.strip()
        for line in args.input.read_text(
            encoding="utf-8"
        ).splitlines()
        if line.strip()
    ]

    if len(
        lines
    ) != 24:
        raise RuntimeError(
            f"Expected exactly 24 captured frames, got {len(lines)}."
        )

    print()
    print("=" * 68)
    print(
        " AUTONOMOUS BASE STATION - REAL ESP32 WINDOW -> PICO AI"
    )
    print("=" * 68)
    print(
        f"Pico endpoint        : {args.url}"
    )
    print(
        f"Frames               : {len(lines)}"
    )
    print(
        f"Chunking             : {args.chunk_size} bytes / "
        f"{args.chunk_delay * 1000.0:.0f} ms"
    )
    print()

    port = serial.serial_for_url(
        args.url,
        baudrate=115200,
        timeout=0.25,
    )

    time.sleep(
        0.5
    )

    port.reset_input_buffer()

    final_result = None

    try:
        for index, line in enumerate(
            lines,
            start=1,
        ):
            payload = (
                line +
                "\n"
            ).encode(
                "utf-8"
            )

            send_chunked(
                port,
                payload,
                args.chunk_size,
                args.chunk_delay,
            )

            result = wait_result(
                port
            )

            if result is None:
                print(
                    f"[FAIL] frame {index:02d}: no PICO_RESULT"
                )

                return 2

            if result.get(
                "telemetry_ok"
            ) is not True:
                print(
                    f"[FAIL] frame {index:02d}: "
                    f"{result.get('reason')}"
                )

                return 3

            count = result.get(
                "window_count"
            )

            ready = result.get(
                "window_ready"
            )

            if ready:
                print(
                    f"[FRAME {index:02d}/24] "
                    f"window={count}/24 READY | "
                    f"AI={result.get('fault_domain')} "
                    f"({result.get('confidence', 0.0):.4f}) | "
                    f"mode={result.get('recommended_mode')}"
                )

                final_result = result
            else:
                print(
                    f"[FRAME {index:02d}/24] "
                    f"window={count}/24 WARMUP"
                )

    finally:
        port.close()

    print()

    if final_result is None:
        print(
            "FAIL - PICO NEVER REACHED AI READY STATE"
        )

        return 4

    print("=" * 68)
    print(
        " PASS - REAL ESP32 120-SECOND WINDOW RAN ON PICO EMBEDDED AI"
    )
    print("=" * 68)
    print(
        f"Embedded model       : {final_result.get('ai_model')}"
    )
    print(
        f"Pico fault domain    : {final_result.get('fault_domain')}"
    )
    print(
        f"Confidence           : {final_result.get('confidence', 0.0):.4f}"
    )
    print(
        f"Pico recommended mode: {final_result.get('recommended_mode')}"
    )
    print(
        f"Source reference     : {final_result.get('source_fault_label')}"
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(
        main()
    )