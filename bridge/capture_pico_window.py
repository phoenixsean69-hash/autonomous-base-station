#!/usr/bin/env python3
"""Capture one real 24-frame / 120-second ESP32 temporal window for Pico replay."""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import serial


ROOT = Path(
    __file__
).resolve().parents[1]

PREFIX = "ABS_JSON|"
SCHEMA = "abs.v1"
DEFAULT_URL = "rfc2217://localhost:4001"
DEFAULT_OUTPUT = (
    ROOT /
    "bridge" /
    "runtime" /
    "pico_temporal_window.jsonl"
)


class Sampler:
    def __init__(
        self,
        interval_ms: int,
    ) -> None:
        self.interval = interval_ms
        self.next_target = None
        self.previous = None

    def accept(
        self,
        timestamp_ms: int,
    ) -> bool:
        value = int(
            timestamp_ms
        )

        if (
            self.previous is not None and
            value < self.previous
        ):
            self.next_target = None

        self.previous = value

        if self.next_target is None:
            self.next_target = (
                value +
                self.interval
            )

            return True

        if value < self.next_target:
            return False

        while (
            self.next_target <= value
        ):
            self.next_target += (
                self.interval
            )

        return True


def main() -> int:
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--url",
        default=DEFAULT_URL,
    )

    parser.add_argument(
        "--frames",
        type=int,
        default=24,
    )

    parser.add_argument(
        "--sample-ms",
        type=int,
        default=5000,
    )

    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
    )

    args = parser.parse_args()

    metadata_path = (
        ROOT /
        "ml" /
        "models" /
        "pico_temporal_student_v1_metadata.json"
    )

    if not metadata_path.exists():
        raise FileNotFoundError(
            "Pico student metadata not found. Run training/export first."
        )

    metadata = json.loads(
        metadata_path.read_text(
            encoding="utf-8"
        )
    )

    features = metadata[
        "feature_columns"
    ]

    args.output.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    print()
    print("=" * 68)
    print(
        " AUTONOMOUS BASE STATION - CAPTURE REAL PICO TEMPORAL WINDOW"
    )
    print("=" * 68)
    print(
        f"ESP32 endpoint       : {args.url}"
    )
    print(
        f"Frames               : {args.frames}"
    )
    print(
        f"Target cadence       : {args.sample_ms / 1000.0:.1f} s"
    )
    print(
        f"Output               : {args.output}"
    )
    print()

    port = serial.serial_for_url(
        args.url,
        baudrate=115200,
        timeout=0.5,
    )

    time.sleep(
        0.3
    )

    port.reset_input_buffer()

    sampler = Sampler(
        args.sample_ms
    )

    captured: list[str] = []

    try:
        while len(
            captured
        ) < args.frames:
            raw = port.readline()

            if not raw:
                continue

            line = raw.decode(
                "utf-8",
                errors="replace",
            ).strip()

            if not line.startswith(
                PREFIX
            ):
                continue

            try:
                obj = json.loads(
                    line[
                        len(
                            PREFIX
                        ):
                    ]
                )
            except json.JSONDecodeError as exc:
                print(
                    f"[DROP] malformed JSON: {exc}"
                )
                continue

            if obj.get(
                "schema"
            ) != SCHEMA:
                print(
                    "[DROP] unsupported schema"
                )
                continue

            missing = [
                name
                for name in features
                if name not in obj
            ]

            if missing:
                print(
                    "[DROP] missing model features: " +
                    ", ".join(
                        missing
                    )
                )
                continue

            timestamp = int(
                obj[
                    "timestamp_ms"
                ]
            )

            if not sampler.accept(
                timestamp
            ):
                continue

            captured.append(
                line
            )

            print(
                f"[CAPTURE {len(captured):02d}/{args.frames}] "
                f"t={timestamp} ms | "
                f"fault={obj.get('fault_label')} | "
                f"mode={obj.get('operating_mode')}"
            )

    finally:
        port.close()

    args.output.write_text(
        "\n".join(
            captured
        ) +
        "\n",
        encoding="utf-8",
    )

    first = json.loads(
        captured[0][
            len(
                PREFIX
            ):
        ]
    )

    last = json.loads(
        captured[-1][
            len(
                PREFIX
            ):
        ]
    )

    print()
    print(
        "CAPTURE COMPLETE"
    )
    print(
        f"First timestamp      : {first['timestamp_ms']}"
    )
    print(
        f"Last timestamp       : {last['timestamp_ms']}"
    )
    print(
        f"Last source fault    : {last.get('fault_label')}"
    )
    print()
    print(
        "Now STOP the ESP32 Wokwi simulation, START Pico Wokwi,"
    )
    print(
        "then run bridge/replay_pico_window.py"
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(
        main()
    )