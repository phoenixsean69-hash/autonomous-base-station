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


_VALIDATION_METRICS_CACHE = None


def load_validation_metrics() -> dict:
    global _VALIDATION_METRICS_CACHE

    if _VALIDATION_METRICS_CACHE is not None:
        return _VALIDATION_METRICS_CACHE

    def read(name: str) -> dict:
        return json.loads(
            (
                ML /
                "results" /
                name
            ).read_text(
                encoding="utf-8"
            )
        )

    domain = read(
        "fault_domain_temporal_tcn_v3_metrics.json"
    )
    root = read(
        "hierarchical_root_cause_v3_metrics.json"
    )
    anomaly = read(
        "temporal_anomaly_v3_metrics.json"
    )
    unified = read(
        "unified_ai_v3_metrics.json"
    )

    domain_tcn = domain[
        "temporal_tcn"
    ]

    local_tcn = root[
        "local_head"
    ][
        "temporal_tcn"
    ]

    upstream_tcn = root[
        "upstream_head"
    ][
        "temporal_tcn"
    ]

    anomaly_ae = anomaly[
        "temporal_autoencoder"
    ]

    end_to_end = unified[
        "end_to_end_root_cause"
    ]

    _VALIDATION_METRICS_CACHE = {
        "domain_accuracy": float(
            domain_tcn["accuracy"]
        ),
        "domain_balanced_accuracy": float(
            domain_tcn["balanced_accuracy"]
        ),
        "domain_macro_precision": float(
            domain_tcn["macro_precision"]
        ),
        "domain_macro_recall": float(
            domain_tcn["macro_recall"]
        ),
        "domain_macro_f1": float(
            domain_tcn["macro_f1"]
        ),
        "domain_log_loss": float(
            domain_tcn["log_loss"]
        ),
        "domain_selected_epoch": int(
            domain["selected_epoch"]
        ),
        "domain_best_validation_loss": float(
            domain["best_validation_loss"]
        ),
        "domain_confusion_matrix": (
            domain_tcn[
                "confusion_matrix"
            ]
        ),
        "local_head_accuracy": float(
            local_tcn["accuracy"]
        ),
        "local_head_balanced_accuracy": float(
            local_tcn["balanced_accuracy"]
        ),
        "local_head_macro_f1": float(
            local_tcn["macro_f1"]
        ),
        "local_head_log_loss": float(
            local_tcn["log_loss"]
        ),
        "local_head_confusion_matrix": (
            local_tcn[
                "confusion_matrix"
            ]
        ),
        "upstream_head_accuracy": float(
            upstream_tcn["accuracy"]
        ),
        "upstream_head_balanced_accuracy": float(
            upstream_tcn["balanced_accuracy"]
        ),
        "upstream_head_macro_f1": float(
            upstream_tcn["macro_f1"]
        ),
        "upstream_head_log_loss": float(
            upstream_tcn["log_loss"]
        ),
        "upstream_head_confusion_matrix": (
            upstream_tcn[
                "confusion_matrix"
            ]
        ),
        "local_e2e_accuracy": float(
            end_to_end[
                "local_accuracy_including_domain_routing"
            ]
        ),
        "upstream_e2e_accuracy": float(
            end_to_end[
                "upstream_accuracy_including_domain_routing"
            ]
        ),
        "mixed_exact_accuracy": float(
            end_to_end[
                "mixed_exact_domain_and_both_causes_accuracy"
            ]
        ),
        "hierarchy_exact_accuracy": float(
            end_to_end[
                "all_sequence_exact_hierarchical_accuracy"
            ]
        ),
        "anomaly_threshold": float(
            anomaly_ae["threshold"]
        ),
        "anomaly_roc_auc": float(
            anomaly_ae["roc_auc"]
        ),
        "anomaly_average_precision": float(
            anomaly_ae["average_precision"]
        ),
        "anomaly_fpr": float(
            anomaly_ae[
                "normal_false_positive_rate"
            ]
        ),
        "anomaly_detection_rate": float(
            anomaly_ae[
                "known_fault_detection_rate"
            ]
        ),
        "anomaly_confusion_matrix": (
            anomaly_ae[
                "binary_confusion_matrix"
            ]
        ),
        "test_rows": int(
            unified["test_rows"]
        ),
        "energy_distribution": (
            unified[
                "energy_recommendation_distribution"
            ]
        ),
    }

    return _VALIDATION_METRICS_CACHE


def probability_or_zero(
    head: dict | None,
    label: str,
) -> float:
    if not head:
        return 0.0

    return float(
        head.get(
            "probabilities",
            {},
        ).get(
            label,
            0.0,
        )
    )


def build_ai_command(
    result: dict,
    *,
    inference_ms: float = 0.0,
    window_count: int = 24,
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

    validation = load_validation_metrics()

    domain_probs = domain[
        "probabilities"
    ]

    context = energy[
        "latest_operating_context"
    ]

    return {
        "schema": "abs.ai.cmd.v1",
        "recommended_mode": (
            energy[
                "recommended_mode"
            ]
        ),
        "reason": reason,
        "diagnostic_state": (
            result[
                "diagnostic_state"
            ]
        ),
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

        # Full live domain probability vector.
        "prob_normal": float(
            domain_probs["NORMAL"]
        ),
        "prob_local": float(
            domain_probs["LOCAL"]
        ),
        "prob_upstream": float(
            domain_probs["UPSTREAM"]
        ),
        "prob_mixed": float(
            domain_probs["MIXED"]
        ),

        # Full live anomaly metrics.
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
        "anomaly_threshold": float(
            anomaly[
                "threshold"
            ]
        ),
        "anomaly_ratio": float(
            anomaly[
                "score_to_threshold_ratio"
            ]
        ),

        # Selected hierarchical root causes.
        "local_root_cause": (
            local["label"]
            if local is not None
            else "NOT_APPLICABLE"
        ),
        "local_root_confidence": (
            float(
                local["confidence"]
            )
            if local is not None
            else 0.0
        ),
        "upstream_root_cause": (
            upstream["label"]
            if upstream is not None
            else "NOT_APPLICABLE"
        ),
        "upstream_root_confidence": (
            float(
                upstream["confidence"]
            )
            if upstream is not None
            else 0.0
        ),

        # All local-head probabilities.
        "local_prob_cooling": probability_or_zero(
            local,
            "COOLING_FAULT",
        ),
        "local_prob_radio": probability_or_zero(
            local,
            "RADIO_FAULT",
        ),
        "local_prob_rectifier": probability_or_zero(
            local,
            "RECTIFIER_FAULT",
        ),
        "local_prob_rf_mismatch": probability_or_zero(
            local,
            "RF_MISMATCH",
        ),
        "local_prob_battery_low": probability_or_zero(
            local,
            "BATTERY_LOW",
        ),
        "local_prob_grid_failure": probability_or_zero(
            local,
            "GRID_FAILURE",
        ),
        "local_prob_vibration": probability_or_zero(
            local,
            "MECHANICAL_VIBRATION",
        ),
        "local_prob_traffic": probability_or_zero(
            local,
            "TRAFFIC_OVERLOAD",
        ),

        # All upstream-head probabilities.
        "upstream_prob_congestion": probability_or_zero(
            upstream,
            "BACKHAUL_CONGESTION",
        ),
        "upstream_prob_degradation": probability_or_zero(
            upstream,
            "UPSTREAM_LINK_DEGRADATION",
        ),
        "upstream_prob_failure": probability_or_zero(
            upstream,
            "UPSTREAM_LINK_FAILURE",
        ),
        "upstream_prob_outage": probability_or_zero(
            upstream,
            "UPSTREAM_OUTAGE",
        ),

        # Runtime/window metrics.
        "inference_ms": float(
            inference_ms
        ),
        "window_count": float(
            window_count
        ),
        "window_seconds": float(
            result[
                "window"
            ][
                "window_seconds"
            ]
        ),

        # Live context used by the recommendation layer.
        "context_traffic_pct": float(
            context[
                "traffic_load_pct"
            ]
        ),
        "context_battery_soc_pct": float(
            context[
                "battery_soc_pct"
            ]
        ),
        "context_grid_available": bool(
            context[
                "grid_available"
            ]
        ),
        "context_generator_running": bool(
            context[
                "generator_running"
            ]
        ),

        # Real held-out model validation metrics.
        "model_domain_accuracy": (
            validation[
                "domain_accuracy"
            ]
        ),
        "model_domain_balanced_accuracy": (
            validation[
                "domain_balanced_accuracy"
            ]
        ),
        "model_domain_macro_f1": (
            validation[
                "domain_macro_f1"
            ]
        ),
        "model_domain_log_loss": (
            validation[
                "domain_log_loss"
            ]
        ),
        "model_local_head_accuracy": (
            validation[
                "local_head_accuracy"
            ]
        ),
        "model_upstream_head_accuracy": (
            validation[
                "upstream_head_accuracy"
            ]
        ),
        "model_local_e2e_accuracy": (
            validation[
                "local_e2e_accuracy"
            ]
        ),
        "model_upstream_e2e_accuracy": (
            validation[
                "upstream_e2e_accuracy"
            ]
        ),
        "model_mixed_exact_accuracy": (
            validation[
                "mixed_exact_accuracy"
            ]
        ),
        "model_hierarchy_accuracy": (
            validation[
                "hierarchy_exact_accuracy"
            ]
        ),
        "model_anomaly_roc_auc": (
            validation[
                "anomaly_roc_auc"
            ]
        ),
        "model_anomaly_average_precision": (
            validation[
                "anomaly_average_precision"
            ]
        ),
        "model_anomaly_fpr": (
            validation[
                "anomaly_fpr"
            ]
        ),
        "model_anomaly_detection_rate": (
            validation[
                "anomaly_detection_rate"
            ]
        ),
    }


def print_model_validation_report() -> None:
    m = load_validation_metrics()

    print()
    print("=" * 70)
    print(" TRAINED MODEL VALIDATION METRICS - HELD-OUT TEST SET")
    print("=" * 70)

    print(
        f"Test sequences         : {m['test_rows']}"
    )
    print()
    print("[ Fault-domain TCN ]")
    print(
        f"Accuracy               : {m['domain_accuracy'] * 100:.2f}%"
    )
    print(
        f"Balanced accuracy      : {m['domain_balanced_accuracy'] * 100:.2f}%"
    )
    print(
        f"Macro precision        : {m['domain_macro_precision'] * 100:.2f}%"
    )
    print(
        f"Macro recall           : {m['domain_macro_recall'] * 100:.2f}%"
    )
    print(
        f"Macro F1               : {m['domain_macro_f1'] * 100:.2f}%"
    )
    print(
        f"Log loss               : {m['domain_log_loss']:.4f}"
    )
    print(
        f"Selected epoch         : {m['domain_selected_epoch']}"
    )
    print(
        f"Best validation loss   : {m['domain_best_validation_loss']:.4f}"
    )
    print(
        "Confusion matrix       : " +
        json.dumps(
            m["domain_confusion_matrix"]
        )
    )

    print()
    print("[ Root-cause heads - conditioned ]")
    print(
        f"Local accuracy         : {m['local_head_accuracy'] * 100:.2f}%"
    )
    print(
        f"Local balanced acc     : {m['local_head_balanced_accuracy'] * 100:.2f}%"
    )
    print(
        f"Local macro F1         : {m['local_head_macro_f1'] * 100:.2f}%"
    )
    print(
        f"Local log loss         : {m['local_head_log_loss']:.4f}"
    )
    print(
        "Local confusion       : " +
        json.dumps(
            m["local_head_confusion_matrix"]
        )
    )
    print(
        f"Upstream accuracy      : {m['upstream_head_accuracy'] * 100:.2f}%"
    )
    print(
        f"Upstream balanced acc  : {m['upstream_head_balanced_accuracy'] * 100:.2f}%"
    )
    print(
        f"Upstream macro F1      : {m['upstream_head_macro_f1'] * 100:.2f}%"
    )
    print(
        f"Upstream log loss      : {m['upstream_head_log_loss']:.4f}"
    )
    print(
        "Upstream confusion    : " +
        json.dumps(
            m["upstream_head_confusion_matrix"]
        )
    )

    print()
    print("[ End-to-end hierarchy ]")
    print(
        f"Local incl routing     : {m['local_e2e_accuracy'] * 100:.2f}%"
    )
    print(
        f"Upstream incl routing  : {m['upstream_e2e_accuracy'] * 100:.2f}%"
    )
    print(
        f"Mixed exact both       : {m['mixed_exact_accuracy'] * 100:.2f}%"
    )
    print(
        f"All-sequence exact     : {m['hierarchy_exact_accuracy'] * 100:.2f}%"
    )

    print()
    print("[ Temporal anomaly autoencoder ]")
    print(
        f"Threshold              : {m['anomaly_threshold']:.6f}"
    )
    print(
        f"ROC-AUC                : {m['anomaly_roc_auc']:.4f}"
    )
    print(
        f"Average precision      : {m['anomaly_average_precision']:.4f}"
    )
    print(
        f"Normal false positive  : {m['anomaly_fpr'] * 100:.2f}%"
    )
    print(
        f"Known-fault detection  : {m['anomaly_detection_rate'] * 100:.2f}%"
    )
    print(
        "Binary confusion      : " +
        json.dumps(
            m["anomaly_confusion_matrix"]
        )
    )

    print()
    print("[ Energy recommendations on held-out set ]")
    for mode, count in m[
        "energy_distribution"
    ].items():
        print(
            f"{mode:22s}: {count}"
        )

    print()
    print(
        "NOTE: Validation metrics above are fixed held-out model metrics; "
        "they are not live confidence values."
    )


def print_live_ai_report(
    result: dict,
    *,
    inference_ms: float,
    window_count: int,
) -> None:
    domain = result["fault_domain"]
    anomaly = result["anomaly"]
    local = result["root_cause"]["local"]
    upstream = result["root_cause"]["upstream"]
    energy = result["energy_recommendation"]
    context = energy["latest_operating_context"]

    print()
    print("-" * 70)
    print(" LIVE AI INFERENCE - REAL MODEL OUTPUT")
    print("-" * 70)

    print(
        f"Diagnostic state       : {result['diagnostic_state']}"
    )
    print(
        f"Fault domain           : {domain['label']}"
    )
    print(
        f"Domain confidence      : {domain['confidence'] * 100:.2f}%"
    )

    print()
    print("Domain probabilities:")
    for label in [
        "NORMAL",
        "LOCAL",
        "UPSTREAM",
        "MIXED",
    ]:
        print(
            f"  {label:18s}: "
            f"{domain['probabilities'][label] * 100:.2f}%"
        )

    print()
    print("Anomaly detector:")
    print(
        f"  Flagged              : {'YES' if anomaly['flagged'] else 'NO'}"
    )
    print(
        f"  Score                : {anomaly['score']:.6f}"
    )
    print(
        f"  Threshold            : {anomaly['threshold']:.6f}"
    )
    print(
        f"  Score/threshold      : {anomaly['score_to_threshold_ratio']:.4f}x"
    )

    print()
    print("Local root-cause head:")
    if local is None:
        print("  Result               : NOT APPLICABLE")
    else:
        print(
            f"  Winner               : {local['label']}"
        )
        print(
            f"  Confidence           : {local['confidence'] * 100:.2f}%"
        )
        for label, probability in local[
            "probabilities"
        ].items():
            print(
                f"  {label:20s}: {probability * 100:.2f}%"
            )

    print()
    print("Upstream root-cause head:")
    if upstream is None:
        print("  Result               : NOT APPLICABLE")
    else:
        print(
            f"  Winner               : {upstream['label']}"
        )
        print(
            f"  Confidence           : {upstream['confidence'] * 100:.2f}%"
        )
        for label, probability in upstream[
            "probabilities"
        ].items():
            print(
                f"  {label:28s}: {probability * 100:.2f}%"
            )

    print()
    print("AI energy recommendation:")
    print(
        f"  Recommended mode     : {energy['recommended_mode']}"
    )
    print(
        f"  Reason               : {energy['reason']}"
    )
    print(
        f"  Traffic used         : {context['traffic_load_pct']:.2f}%"
    )
    print(
        f"  Battery SoC used     : {context['battery_soc_pct']:.2f}%"
    )
    print(
        f"  Grid available       : {context['grid_available']}"
    )
    print(
        f"  Generator running    : {context['generator_running']}"
    )

    print()
    print("Runtime:")
    print(
        f"  Window               : {window_count}/{result['window']['timesteps']} frames"
    )
    print(
        f"  Window duration      : {result['window']['window_seconds']:.0f} s"
    )
    print(
        f"  Inference time       : {inference_ms:.2f} ms"
    )


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
        result,
        inference_ms=inference_ms,
        window_count=window.count,
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

    print_model_validation_report()

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
                result,
                inference_ms=inference_ms,
                window_count=window.count,
            )

            print_live_ai_report(
                result,
                inference_ms=inference_ms,
                window_count=window.count,
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