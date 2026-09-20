#!/usr/bin/env python3
"""Closed-loop ESP32 temporal AI bridge.

ESP32 ABS_JSON
    -> 5-second sampler
    -> 24-frame temporal AI window
    -> UnifiedAIEngine
    -> ABS_AI_CMD written back to the same ESP32 serial link
    -> deterministic ESP32 guardrails
    -> final operating mode reported in later ABS_JSON packets

Rule outputs are reference-only and NEVER enter the 33 trained AI features.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    print(
        r"PySerial missing. Install with "
        r".\.venv\Scripts\python.exe -m pip install pyserial"
    )
    raise SystemExit(1)


ROOT = Path(__file__).resolve().parents[1]
ML = ROOT / "ml"

sys.path.insert(
    0,
    str(ML),
)

from unified_ai_inference import UnifiedAIEngine  # noqa: E402


TELEMETRY_PREFIX = "ABS_JSON|"
AI_RESULT_PREFIX = "ABS_AI_RESULT|"
AI_COMMAND_PREFIX = "ABS_AI_CMD|"
AI_ACK_PREFIX = "ABS_AI_ACK|"

SCHEMA = "abs.v1"
DEFAULT_URL = "rfc2217://localhost:4001"


class Sampler:
    def __init__(
        self,
        interval_ms: int,
    ) -> None:
        self.interval = int(
            interval_ms
        )

        self.next_target = None
        self.previous = None

    def accept(
        self,
        timestamp_ms: int,
    ) -> bool:
        timestamp = int(
            timestamp_ms
        )

        if (
            self.previous is not None and
            timestamp < self.previous
        ):
            self.next_target = None

        self.previous = timestamp

        if self.next_target is None:
            self.next_target = (
                timestamp +
                self.interval
            )

            return True

        if timestamp < self.next_target:
            return False

        while (
            self.next_target <=
            timestamp
        ):
            self.next_target += (
                self.interval
            )

        return True


def parse_telemetry(
    line: str,
) -> dict:
    if not line.startswith(
        TELEMETRY_PREFIX
    ):
        raise ValueError(
            "missing ABS_JSON prefix"
        )

    telemetry = json.loads(
        line[
            len(
                TELEMETRY_PREFIX
            ):
        ]
    )

    if telemetry.get(
        "schema"
    ) != SCHEMA:
        raise ValueError(
            "unsupported telemetry schema"
        )

    return telemetry


def check_features(
    telemetry: dict,
    features: list[str],
) -> None:
    missing = [
        feature
        for feature in features
        if feature not in telemetry
    ]

    if missing:
        raise KeyError(
            "missing AI features: " +
            ", ".join(
                missing
            )
        )


def summary(
    result: dict,
) -> str:
    domain = result[
        "fault_domain"
    ]

    anomaly = result[
        "anomaly"
    ]

    energy = result[
        "energy_recommendation"
    ]

    parts = [
        (
            f"domain={domain['label']} "
            f"({domain['confidence']:.3f})"
        ),
        (
            f"anomaly="
            f"{'YES' if anomaly['flagged'] else 'NO'} "
            f"score={anomaly['score']:.3f}"
        ),
    ]

    local = result[
        "root_cause"
    ][
        "local"
    ]

    upstream = result[
        "root_cause"
    ][
        "upstream"
    ]

    if local:
        parts.append(
            (
                f"local={local['label']} "
                f"({local['confidence']:.3f})"
            )
        )

    if upstream:
        parts.append(
            (
                f"upstream={upstream['label']} "
                f"({upstream['confidence']:.3f})"
            )
        )

    parts.append(
        "AI-mode=" +
        energy[
            "recommended_mode"
        ]
    )

    return " | ".join(
        parts
    )


def build_ai_command(
    result: dict,
) -> dict:
    domain = result[
        "fault_domain"
    ]

    anomaly = result[
        "anomaly"
    ]

    energy = result[
        "energy_recommendation"
    ]

    reason = str(
        energy[
            "reason"
        ]
    )

    prefix = (
        "AI PRE-GUARDRAIL: "
    )

    if reason.startswith(
        prefix
    ):
        reason = reason[
            len(prefix):
        ]

    return {
        "schema": "abs.ai.cmd.v1",
        "recommended_mode": (
            energy[
                "recommended_mode"
            ]
        ),
        "reason": reason,
        "fault_domain": (
            domain[
                "label"
            ]
        ),
        "domain_confidence": float(
            domain[
                "confidence"
            ]
        ),
        "anomaly_flag": bool(
            anomaly[
                "flagged"
            ]
        ),
        "anomaly_score": float(
            anomaly[
                "score"
            ]
        ),
    }


def send_ai_command(
    port,
    command: dict,
) -> None:
    line = (
        AI_COMMAND_PREFIX +
        json.dumps(
            command,
            separators=(
                ",",
                ":",
            ),
        ) +
        "\n"
    )

    port.write(
        line.encode(
            "utf-8"
        )
    )

    port.flush()


def save_latest(
    result: dict,
) -> Path:
    directory = (
        ROOT /
        "bridge" /
        "runtime"
    )

    directory.mkdir(
        parents=True,
        exist_ok=True,
    )

    target = (
        directory /
        "latest_ai_result.json"
    )

    temporary = (
        directory /
        "latest_ai_result.json.tmp"
    )

    temporary.write_text(
        json.dumps(
            result,
            indent=2,
        ),
        encoding="utf-8",
    )

    temporary.replace(
        target
    )

    return target


def smoke(
    engine: UnifiedAIEngine,
) -> int:
    line = (
        ROOT /
        "bridge" /
        "captured_esp32_packet.txt"
    ).read_text(
        encoding="utf-8"
    ).strip()

    telemetry = parse_telemetry(
        line
    )

    check_features(
        telemetry,
        engine.feature_columns,
    )

    window = engine.new_buffer()

    for _ in range(
        engine.timesteps
    ):
        window.add(
            telemetry
        )

    started = time.perf_counter()

    result = engine.diagnose(
        window.as_array()
    )

    inference_ms = (
        (
            time.perf_counter() -
            started
        ) *
        1000.0
    )

    command = build_ai_command(
        result
    )

    print()
    print(
        "[ CLOSED-LOOP PROTOCOL SMOKE TEST ]"
    )
    print(
        "Captured packet      : PASS"
    )
    print(
        f"AI features          : "
        f"{len(engine.feature_columns)}/"
        f"{len(engine.feature_columns)}"
    )
    print(
        f"Temporal buffer      : "
        f"{window.count}/{engine.timesteps} READY"
    )
    print(
        f"Inference            : PASS "
        f"({inference_ms:.1f} ms)"
    )
    print(
        "AI command schema    : abs.ai.cmd.v1"
    )
    print(
        "Recommended mode     : " +
        command[
            "recommended_mode"
        ]
    )
    print(
        "Guardrail authority  : ESP32 DETERMINISTIC"
    )

    print(
        AI_COMMAND_PREFIX +
        json.dumps(
            command,
            separators=(
                ",",
                ":",
            ),
        )
    )

    return 0


def live(
    engine: UnifiedAIEngine,
    url: str,
    sample_ms: int,
) -> int:
    window = engine.new_buffer()
    sampler = Sampler(
        sample_ms
    )

    print()
    print("=" * 70)
    print(
        " AUTONOMOUS BASE STATION - CLOSED-LOOP TEMPORAL AI"
    )
    print("=" * 70)
    print(
        f"Endpoint             : {url}"
    )
    print(
        f"Temporal cadence     : "
        f"{sample_ms / 1000.0:.1f} s"
    )
    print(
        f"Window               : "
        f"{engine.timesteps} frames / "
        f"{engine.timesteps * engine.sample_interval_seconds:.0f} s"
    )
    print(
        "AI command           : ENABLED"
    )
    print(
        "Final mode authority : ESP32 DETERMINISTIC GUARDRAILS"
    )
    print(
        "Rule outputs          : REFERENCE ONLY - NOT AI FEATURES"
    )
    print(
        "Press Ctrl+C to stop."
    )
    print()

    port = serial.serial_for_url(
        url,
        baudrate=115200,
        timeout=0.50,
    )

    time.sleep(
        0.3
    )

    port.reset_input_buffer()

    print(
        "[OK] ESP32 connected."
    )

    source = 0
    sampled = 0
    inferences = 0
    rejected = 0
    commands_sent = 0
    commands_acked = 0
    commands_rejected = 0

    try:
        while True:
            raw = port.readline()

            if not raw:
                continue

            line = raw.decode(
                "utf-8",
                errors="replace",
            ).strip()

            if line.startswith(
                AI_ACK_PREFIX
            ):
                try:
                    ack = json.loads(
                        line[
                            len(
                                AI_ACK_PREFIX
                            ):
                        ]
                    )
                except json.JSONDecodeError:
                    print(
                        "[AI ACK] malformed JSON"
                    )
                    continue

                if ack.get(
                    "accepted"
                ) is True:
                    commands_acked += 1

                    print(
                        "  [AI ACK] ACCEPTED | "
                        f"mode={ack.get('recommended_mode')}"
                    )
                else:
                    commands_rejected += 1

                    print(
                        "  [AI ACK] REJECTED | "
                        f"reason={ack.get('reason')}"
                    )

                continue

            if not line.startswith(
                TELEMETRY_PREFIX
            ):
                continue

            source += 1

            try:
                telemetry = parse_telemetry(
                    line
                )

                check_features(
                    telemetry,
                    engine.feature_columns,
                )

                timestamp_ms = int(
                    telemetry[
                        "timestamp_ms"
                    ]
                )

            except (
                ValueError,
                KeyError,
                TypeError,
                json.JSONDecodeError,
            ) as exc:
                rejected += 1

                print(
                    f"[DROP] {exc}"
                )

                continue

            ai_status = telemetry.get(
                "ai_command_status",
                "NOT_SUPPORTED_YET",
            )

            ai_mode_seen = telemetry.get(
                "ai_recommended_mode",
                "NONE",
            )

            final_mode_seen = telemetry.get(
                "operating_mode",
                "UNKNOWN",
            )

            guardrail_seen = telemetry.get(
                "guardrail_status",
                "UNKNOWN",
            )

            if (
                ai_status !=
                "NOT_SUPPORTED_YET"
            ):
                print(
                    "  [CONTROL FEEDBACK] "
                    f"AI={ai_status}/{ai_mode_seen} | "
                    f"FINAL={final_mode_seen} | "
                    f"GUARD={guardrail_seen}"
                )

            if not sampler.accept(
                timestamp_ms
            ):
                continue

            sampled += 1

            window.add(
                telemetry
            )

            ref_fault = telemetry.get(
                "fault_label",
                "UNKNOWN",
            )

            print(
                f"[SAMPLE {sampled:04d}] "
                f"t={timestamp_ms} ms | "
                f"window={window.count}/{engine.timesteps} | "
                f"source-ref fault={ref_fault}"
            )

            if not window.ready:
                print(
                    f"  AI WARMUP: "
                    f"{window.count}/{engine.timesteps}"
                )

                continue

            started = time.perf_counter()

            result = engine.diagnose(
                window.as_array()
            )

            inference_ms = (
                (
                    time.perf_counter() -
                    started
                ) *
                1000.0
            )

            inferences += 1

            command = build_ai_command(
                result
            )

            send_ai_command(
                port,
                command,
            )

            commands_sent += 1

            enriched = dict(
                result
            )

            enriched[
                "runtime"
            ] = {
                "source_schema": SCHEMA,
                "source_timestamp_ms": (
                    timestamp_ms
                ),
                "accepted_temporal_samples": (
                    sampled
                ),
                "inference_ms": (
                    inference_ms
                ),
                "ai_command": (
                    command
                ),
                "ai_command_sent": True,
                "final_mode_authority": (
                    "ESP32_DETERMINISTIC_GUARDRAILS"
                ),
                "source_reference_only": {
                    "fault_label": (
                        ref_fault
                    ),
                    "operating_mode": (
                        final_mode_seen
                    ),
                    "guardrail_status": (
                        guardrail_seen
                    ),
                },
                "source_reference_used_as_ai_features": (
                    False
                ),
            }

            target = save_latest(
                enriched
            )

            print(
                f"  [AI #{inferences}] "
                f"{summary(result)} | "
                f"{inference_ms:.1f} ms"
            )

            print(
                "  [AI CMD] SENT -> "
                f"{command['recommended_mode']} | "
                f"{command['reason']}"
            )

            print(
                AI_RESULT_PREFIX +
                json.dumps(
                    enriched,
                    separators=(
                        ",",
                        ":",
                    ),
                )
            )

            print(
                f"  Latest result: {target}"
            )

    except KeyboardInterrupt:
        print(
            "\nClosed-loop AI stopped by user."
        )

    finally:
        port.close()

    print()
    print(
        "[ CLOSED-LOOP STATISTICS ]"
    )
    print(
        f"ESP32 packets read : {source}"
    )
    print(
        f"Temporal samples   : {sampled}"
    )
    print(
        f"AI inferences      : {inferences}"
    )
    print(
        f"Commands sent      : {commands_sent}"
    )
    print(
        f"Commands accepted  : {commands_acked}"
    )
    print(
        f"Commands rejected  : {commands_rejected}"
    )
    print(
        f"Rejected telemetry : {rejected}"
    )

    return 0


def main() -> int:
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--url",
        default=DEFAULT_URL,
    )

    parser.add_argument(
        "--sample-ms",
        type=int,
        default=5000,
    )

    parser.add_argument(
        "--smoke-test",
        action="store_true",
    )

    args = parser.parse_args()

    engine = UnifiedAIEngine(
        ML
    )

    if args.smoke_test:
        return smoke(
            engine
        )

    return live(
        engine,
        args.url,
        args.sample_ms,
    )


if __name__ == "__main__":
    raise SystemExit(
        main()
    )