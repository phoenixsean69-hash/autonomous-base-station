#!/usr/bin/env python3
"""
Harder V2 synthetic dataset for the Autonomous Base Station hybrid AI stage.

V1 proved the pipeline but was intentionally clean enough that Random Forest
reached 100% accuracy. V2 creates overlapping and ambiguous operating states:

- healthy-but-stressed NORMAL samples
- mild and early-stage faults
- imperfect/lagging digital status bits
- cross-domain secondary symptoms
- recovery/onset-like partial fault expression
- benign transient network disturbances
- realistic measurement noise
- mixed faults where one side may be subtle

The output keeps the SAME 33 physical/telemetry features as V1 so Random
Forest, XGBoost and deep-learning models can be compared fairly.

Rule-engine outputs remain excluded from the model inputs.
"""

from __future__ import annotations

import argparse
import csv
import itertools
import json
import random
from collections import Counter
from pathlib import Path

import generate_dataset as v1


STATUS_KEYS = {
    "physical_link_up",
    "upstream_reachable",
    "grid_available",
    "generator_running",
    "fan_operational",
    "rectifier_normal",
    "radio_operational",
}

PRIMARY_NOISE_SIGMA = {
    "shelter_temp_c": 0.8,
    "humidity_pct": 1.8,
    "pa_temp_c": 1.1,
    "pa_temp_trend_c_per_min": 0.18,
    "vibration_rms_mps2": 0.07,
    "vibration_std_mps2": 0.04,
    "vibration_peak_to_peak_mps2": 0.16,
    "vibration_dominant_hz": 0.8,
    "dc_voltage_v": 0.22,
    "dc_current_a": 0.18,
    "battery_soc_pct": 0.8,
    "battery_soc_trend_pct_per_min": 0.07,
    "rf_forward_w": 1.3,
    "rf_reflection_ratio_pct": 0.42,
    "latency_ms": 3.5,
    "latency_jitter_ms": 1.0,
    "packet_loss_pct": 0.25,
    "rssi_dbm": 0.75,
    "rssi_drop_db": 0.35,
    "traffic_load_pct": 1.4,
}


def make_balanced_assignments(
    sample_count: int,
    rng: random.Random,
) -> list[str]:
    domains = [
        "NORMAL",
        "LOCAL",
        "UPSTREAM",
        "MIXED",
    ]

    assignments = [
        domains[index % 4]
        for index in range(sample_count)
    ]

    rng.shuffle(assignments)
    return assignments


def blend_fault_expression(
    baseline: dict,
    faulted: dict,
    strength: float,
    rng: random.Random,
) -> dict:
    """
    Blend a fully generated fault back toward its healthy baseline.

    This imitates early onset, partial degradation, intermittent symptoms,
    and recovery. Digital status bits can lag behind analogue evidence.
    """
    sample = baseline.copy()

    for key, fault_value in faulted.items():
        base_value = baseline[key]

        if key in STATUS_KEYS:
            if fault_value != base_value:
                flip_probability = (
                    0.10 +
                    0.72 * strength
                )

                sample[key] = (
                    fault_value
                    if rng.random() < flip_probability
                    else base_value
                )
            else:
                sample[key] = base_value
            continue

        if isinstance(base_value, (int, float)):
            sample[key] = (
                base_value +
                (fault_value - base_value) * strength
            )
        else:
            sample[key] = fault_value

    return sample


def apply_normal_stress(
    sample: dict,
    rng: random.Random,
    counters: Counter,
) -> None:
    """
    NORMAL does not mean every sensor sits at an ideal textbook value.

    These are healthy but stressed operating conditions designed to overlap
    with early fault signatures without constituting an active fault.
    """
    profile = rng.choice(
        [
            "BASELINE",
            "HIGH_TRAFFIC_HEALTHY",
            "HOT_DAY_HEALTHY",
            "WEAK_BACKHAUL_HEALTHY",
            "RF_EDGE_HEALTHY",
            "LOW_BATTERY_RESERVE",
        ]
    )

    counters[
        f"normal_profile:{profile}"
    ] += 1

    if profile == "HIGH_TRAFFIC_HEALTHY":
        traffic = rng.uniform(66.0, 88.0)
        sample["traffic_load_pct"] = traffic

        sample["dc_current_a"] += rng.uniform(
            0.7, 2.2
        )

        sample["pa_temp_c"] += rng.uniform(
            2.0, 7.0
        )

        sample["latency_ms"] += rng.uniform(
            4.0, 28.0
        )

        sample["packet_loss_pct"] += rng.uniform(
            0.0, 1.2
        )

    elif profile == "HOT_DAY_HEALTHY":
        sample["shelter_temp_c"] = rng.uniform(
            34.0, 41.0
        )

        sample["pa_temp_c"] = rng.uniform(
            49.0, 67.0
        )

        sample["pa_temp_trend_c_per_min"] = rng.uniform(
            -0.2, 1.2
        )

    elif profile == "WEAK_BACKHAUL_HEALTHY":
        sample["latency_ms"] = rng.uniform(
            58.0, 115.0
        )

        sample["latency_jitter_ms"] = rng.uniform(
            5.0, 18.0
        )

        sample["packet_loss_pct"] = rng.uniform(
            0.8, 4.0
        )

        sample["rssi_dbm"] = rng.uniform(
            -74.0, -63.0
        )

        sample["rssi_drop_db"] = rng.uniform(
            1.0, 5.0
        )

    elif profile == "RF_EDGE_HEALTHY":
        sample["rf_reflection_ratio_pct"] = rng.uniform(
            5.0, 10.5
        )

        sample["rf_forward_w"] *= rng.uniform(
            0.90, 1.02
        )

    elif profile == "LOW_BATTERY_RESERVE":
        sample["battery_soc_pct"] = rng.uniform(
            27.0, 45.0
        )

        sample["battery_soc_trend_pct_per_min"] = rng.uniform(
            -0.22, 0.08
        )

        sample["grid_available"] = 1
        sample["generator_running"] = 0


def add_local_secondary_network_symptoms(
    sample: dict,
    rng: random.Random,
) -> None:
    """
    Some local faults can indirectly affect perceived service quality.
    This prevents 'bad network metrics == upstream' from becoming a shortcut.
    """
    if rng.random() >= 0.34:
        return

    sample["latency_ms"] += rng.uniform(
        8.0, 65.0
    )

    sample["latency_jitter_ms"] += rng.uniform(
        2.0, 14.0
    )

    sample["packet_loss_pct"] += rng.uniform(
        0.2, 4.5
    )

    if rng.random() < 0.45:
        sample["rssi_dbm"] -= rng.uniform(
            0.5, 4.0
        )


def add_upstream_secondary_local_stress(
    sample: dict,
    rng: random.Random,
) -> None:
    """
    Busy/degraded backhaul can coincide with high site load and warmer radio
    equipment. This prevents 'warm/high-current == local' from being trivial.
    """
    if rng.random() >= 0.36:
        return

    sample["traffic_load_pct"] = max(
        sample["traffic_load_pct"],
        rng.uniform(58.0, 92.0),
    )

    sample["dc_current_a"] += rng.uniform(
        0.4, 1.9
    )

    sample["pa_temp_c"] += rng.uniform(
        1.5, 8.0
    )

    sample["pa_temp_trend_c_per_min"] = max(
        sample["pa_temp_trend_c_per_min"],
        rng.uniform(0.1, 1.3),
    )


def add_benign_transient(
    sample: dict,
    rng: random.Random,
    counters: Counter,
) -> None:
    """
    Short transients may occur in any class, including NORMAL.
    """
    if rng.random() >= 0.10:
        return

    transient = rng.choice(
        [
            "LATENCY_SPIKE",
            "LOSS_BURST",
            "RSSI_DIP",
            "RF_BLIP",
            "TEMP_BLIP",
        ]
    )

    counters[
        f"transient:{transient}"
    ] += 1

    if transient == "LATENCY_SPIKE":
        sample["latency_ms"] += rng.uniform(
            18.0, 95.0
        )

        sample["latency_jitter_ms"] += rng.uniform(
            3.0, 20.0
        )

    elif transient == "LOSS_BURST":
        sample["packet_loss_pct"] += rng.uniform(
            1.0, 7.0
        )

    elif transient == "RSSI_DIP":
        sample["rssi_dbm"] -= rng.uniform(
            2.0, 9.0
        )

        sample["rssi_drop_db"] += rng.uniform(
            1.0, 7.0
        )

    elif transient == "RF_BLIP":
        sample["rf_reflection_ratio_pct"] += rng.uniform(
            1.0, 6.0
        )

    elif transient == "TEMP_BLIP":
        sample["pa_temp_c"] += rng.uniform(
            1.0, 5.0
        )


def add_measurement_noise(
    sample: dict,
    rng: random.Random,
) -> None:
    for key, sigma in PRIMARY_NOISE_SIGMA.items():
        if key in sample:
            sample[key] += rng.gauss(
                0.0, sigma
            )

    # Telemetry/status bit noise is intentionally small but non-zero.
    for key in STATUS_KEYS:
        if rng.random() < 0.012:
            sample[key] = (
                0 if sample[key] else 1
            )


def make_fault_sample(
    baseline: dict,
    domain: str,
    local_cause: str,
    upstream_cause: str,
    rng: random.Random,
    counters: Counter,
) -> dict:
    sample = baseline.copy()

    if domain in {
        "LOCAL",
        "MIXED",
    }:
        local_full = sample.copy()

        local_severity = rng.uniform(
            0.08, 0.82
        )

        v1.apply_local_fault(
            local_full,
            local_cause,
            rng,
            local_severity,
        )

        # Most V2 faults are deliberately partial, not textbook full failures.
        local_strength = rng.uniform(
            0.28, 0.92
        )

        if domain == "MIXED" and rng.random() < 0.50:
            local_strength *= rng.uniform(
                0.45, 0.75
            )

        sample = blend_fault_expression(
            sample,
            local_full,
            local_strength,
            rng,
        )

        if local_strength < 0.55:
            counters["mild_local"] += 1

    if domain in {
        "UPSTREAM",
        "MIXED",
    }:
        upstream_full = sample.copy()

        upstream_severity = rng.uniform(
            0.08, 0.82
        )

        v1.apply_upstream_fault(
            upstream_full,
            upstream_cause,
            rng,
            upstream_severity,
        )

        upstream_strength = rng.uniform(
            0.28, 0.92
        )

        if domain == "MIXED" and rng.random() < 0.50:
            upstream_strength *= rng.uniform(
                0.45, 0.75
            )

        sample = blend_fault_expression(
            sample,
            upstream_full,
            upstream_strength,
            rng,
        )

        if upstream_strength < 0.55:
            counters["mild_upstream"] += 1

    if domain == "LOCAL":
        add_local_secondary_network_symptoms(
            sample,
            rng,
        )

    elif domain == "UPSTREAM":
        add_upstream_secondary_local_stress(
            sample,
            rng,
        )

    elif domain == "MIXED":
        # Mixed faults can have both secondary effects, but not always.
        if rng.random() < 0.55:
            add_local_secondary_network_symptoms(
                sample,
                rng,
            )

        if rng.random() < 0.55:
            add_upstream_secondary_local_stress(
                sample,
                rng,
            )

    return sample


def digital_flags_look_healthy(
    sample: dict,
) -> bool:
    return (
        sample["physical_link_up"] == 1 and
        sample["upstream_reachable"] == 1 and
        sample["grid_available"] == 1 and
        sample["fan_operational"] == 1 and
        sample["rectifier_normal"] == 1 and
        sample["radio_operational"] == 1
    )


def generate_dataset_v2(
    sample_count: int,
    seed: int,
) -> tuple[list[dict], Counter]:
    if sample_count < 4:
        raise ValueError(
            "sample_count must be at least 4"
        )

    rng = random.Random(seed)
    counters = Counter()

    assignments = make_balanced_assignments(
        sample_count,
        rng,
    )

    local_cycle = itertools.cycle(
        v1.LOCAL_CAUSES
    )

    upstream_cycle = itertools.cycle(
        v1.UPSTREAM_CAUSES
    )

    mixed_cycle = itertools.cycle(
        list(
            itertools.product(
                v1.LOCAL_CAUSES,
                v1.UPSTREAM_CAUSES,
            )
        )
    )

    rows = []

    for sample_id, domain in enumerate(
        assignments,
        start=1,
    ):
        baseline = v1.make_healthy_baseline(
            rng
        )

        local_cause = "NONE"
        upstream_cause = "NONE"

        if domain == "NORMAL":
            sample = baseline.copy()

            apply_normal_stress(
                sample,
                rng,
                counters,
            )

        elif domain == "LOCAL":
            local_cause = next(
                local_cycle
            )

            sample = make_fault_sample(
                baseline,
                domain,
                local_cause,
                upstream_cause,
                rng,
                counters,
            )

        elif domain == "UPSTREAM":
            upstream_cause = next(
                upstream_cycle
            )

            sample = make_fault_sample(
                baseline,
                domain,
                local_cause,
                upstream_cause,
                rng,
                counters,
            )

        else:
            (
                local_cause,
                upstream_cause,
            ) = next(mixed_cycle)

            sample = make_fault_sample(
                baseline,
                domain,
                local_cause,
                upstream_cause,
                rng,
                counters,
            )

        add_benign_transient(
            sample,
            rng,
            counters,
        )

        add_measurement_noise(
            sample,
            rng,
        )

        v1.finalize_sample(
            sample
        )

        if domain == "NORMAL":
            root_cause = "NORMAL"

        elif domain == "LOCAL":
            root_cause = local_cause

        elif domain == "UPSTREAM":
            root_cause = upstream_cause

        else:
            root_cause = (
                f"{local_cause}+"
                f"{upstream_cause}"
            )

        if digital_flags_look_healthy(
            sample
        ):
            counters[
                f"all_digital_healthy:{domain}"
            ] += 1

        rows.append(
            {
                "sample_id": sample_id,
                "fault_domain": domain,
                "root_cause": root_cause,
                "local_root_cause": local_cause,
                "upstream_root_cause": upstream_cause,
                **sample,
            }
        )

    return rows, counters


def save_csv(
    rows: list[dict],
    output_path: Path,
) -> None:
    output_path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    with output_path.open(
        "w",
        newline="",
        encoding="utf-8",
    ) as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=list(
                rows[0].keys()
            ),
        )

        writer.writeheader()
        writer.writerows(rows)


def save_manifest(
    rows: list[dict],
    feature_columns: list[str],
    counters: Counter,
    seed: int,
    output_path: Path,
) -> None:
    domain_counts = Counter(
        row["fault_domain"]
        for row in rows
    )

    manifest = {
        "schema": "abs.ml.dataset.v2",
        "purpose": (
            "harder overlapping dataset for "
            "hybrid AI model comparison"
        ),
        "seed": seed,
        "samples": len(rows),
        "fault_domains": dict(
            domain_counts
        ),
        "feature_count": len(
            feature_columns
        ),
        "feature_columns": feature_columns,
        "difficulty_mechanisms": [
            "healthy_but_stressed_normal",
            "mild_faults",
            "partial_fault_expression",
            "lagging_digital_status",
            "cross_domain_secondary_symptoms",
            "benign_transients",
            "measurement_noise",
            "subtle_mixed_faults",
        ],
        "diagnostics": dict(
            counters
        ),
    }

    output_path.write_text(
        json.dumps(
            manifest,
            indent=2,
        ),
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--samples",
        type=int,
        default=16000,
    )

    parser.add_argument(
        "--seed",
        type=int,
        default=84,
    )

    parser.add_argument(
        "--output",
        type=Path,
        default=(
            Path(__file__).resolve().parent /
            "data" /
            "base_station_fault_dataset_v2.csv"
        ),
    )

    args = parser.parse_args()

    rows, counters = generate_dataset_v2(
        args.samples,
        args.seed,
    )

    feature_columns = v1.validate_no_leakage(
        rows
    )

    save_csv(
        rows,
        args.output,
    )

    manifest_path = (
        args.output.parent /
        "dataset_v2_manifest.json"
    )

    save_manifest(
        rows,
        feature_columns,
        counters,
        args.seed,
        manifest_path,
    )

    domain_counts = Counter(
        row["fault_domain"]
        for row in rows
    )

    print()
    print(
        "AUTONOMOUS BASE STATION - "
        "HARD DATASET V2"
    )
    print("=" * 58)
    print(
        f"Samples          : {len(rows)}"
    )
    print(
        f"Feature columns  : {len(feature_columns)}"
    )
    print(
        "Leakage guard    : PASS"
    )

    print()
    print("[ FAULT DOMAINS ]")

    for domain in [
        "NORMAL",
        "LOCAL",
        "UPSTREAM",
        "MIXED",
    ]:
        print(
            f"{domain:<16}: "
            f"{domain_counts[domain]}"
        )

    print()
    print(
        "[ AMBIGUITY CHECK - ALL MAJOR "
        "DIGITAL FLAGS STILL LOOK HEALTHY ]"
    )

    for domain in [
        "NORMAL",
        "LOCAL",
        "UPSTREAM",
        "MIXED",
    ]:
        count = counters[
            f"all_digital_healthy:{domain}"
        ]

        denominator = domain_counts[
            domain
        ]

        percentage = (
            100.0 *
            count /
            denominator
        )

        print(
            f"{domain:<16}: "
            f"{count:>5} "
            f"({percentage:5.1f}%)"
        )

    print()
    print("[ DIFFICULTY SIGNALS ]")
    print(
        f"Mild local       : "
        f"{counters['mild_local']}"
    )
    print(
        f"Mild upstream    : "
        f"{counters['mild_upstream']}"
    )

    transient_total = sum(
        count
        for key, count
        in counters.items()
        if key.startswith(
            "transient:"
        )
    )

    print(
        f"Benign transients: "
        f"{transient_total}"
    )

    print()
    print(
        f"Dataset          : "
        f"{args.output.resolve()}"
    )
    print(
        f"Manifest         : "
        f"{manifest_path.resolve()}"
    )
    print()
    print(
        "V2 goal: models should now face "
        "overlap and ambiguity instead of "
        "textbook-separable faults."
    )


if __name__ == "__main__":
    main()