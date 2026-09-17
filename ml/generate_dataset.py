#!/usr/bin/env python3
"""
Autonomous Base Station synthetic fault-scenario generator.

Produces a balanced, noisy, physically correlated dataset for:
  fault_domain: NORMAL, LOCAL, UPSTREAM, MIXED

Root cause is kept separately:
  local_root_cause
  upstream_root_cause

Rule-engine outputs such as fault_label, rf_health, thermal_risk,
operating_mode and guardrail_status are deliberately NOT model inputs.
"""

from __future__ import annotations

import argparse
import csv
import itertools
import json
import math
import random
from collections import Counter
from pathlib import Path


LOCAL_CAUSES = [
    "COOLING_FAULT",
    "RADIO_FAULT",
    "RECTIFIER_FAULT",
    "RF_MISMATCH",
    "BATTERY_LOW",
    "GRID_FAILURE",
    "MECHANICAL_VIBRATION",
    "TRAFFIC_OVERLOAD",
]

UPSTREAM_CAUSES = [
    "BACKHAUL_CONGESTION",
    "UPSTREAM_LINK_DEGRADATION",
    "UPSTREAM_LINK_FAILURE",
    "UPSTREAM_OUTAGE",
]

TARGET_COLUMNS = [
    "fault_domain",
    "root_cause",
    "local_root_cause",
    "upstream_root_cause",
]

FORBIDDEN_LEAKAGE_COLUMNS = {
    "fault_label",
    "rf_health",
    "electrical_health",
    "thermal_risk",
    "backhaul_status",
    "local_site_status",
    "active_power_source",
    "energy_action",
    "requested_mode",
    "operating_mode",
    "guardrail_status",
    "recovery_state",
    "energy_saving_pct",
    "mode_change_count",
}


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def gauss(
    rng: random.Random,
    mean: float,
    sigma: float,
    low: float | None = None,
    high: float | None = None,
) -> float:
    value = rng.gauss(mean, sigma)
    if low is not None:
        value = max(low, value)
    if high is not None:
        value = min(high, value)
    return value


def derive_rf(sample: dict) -> None:
    ratio = clamp(sample["rf_reflection_ratio_pct"], 0.02, 95.0)
    sample["rf_reflection_ratio_pct"] = ratio

    sample["rf_reflected_w"] = (
        sample["rf_forward_w"] * ratio / 100.0
    )

    gamma = math.sqrt(ratio / 100.0)
    gamma = min(gamma, 0.974)

    sample["vswr"] = (
        (1.0 + gamma) /
        (1.0 - gamma)
    )

    sample["return_loss_db"] = (
        -20.0 *
        math.log10(max(gamma, 1e-6))
    )


def make_healthy_baseline(rng: random.Random) -> dict:
    traffic = gauss(rng, 38.0, 18.0, 3.0, 82.0)
    shelter_temp = gauss(rng, 28.5, 3.0, 18.0, 39.0)
    humidity = gauss(rng, 48.0, 12.0, 20.0, 85.0)

    pa_temp = (
        shelter_temp +
        gauss(rng, 13.5, 3.0, 7.0, 22.0)
    )

    dc_current = (
        7.4 +
        0.080 * traffic +
        rng.gauss(0.0, 0.45)
    )

    dc_voltage = gauss(
        rng, 47.8, 0.7, 45.5, 50.5
    )

    battery_soc = gauss(
        rng, 72.0, 15.0, 35.0, 100.0
    )

    battery_voltage = clamp(
        11.40 +
        0.0102 * battery_soc +
        rng.gauss(0.0, 0.035),
        11.7,
        12.6,
    )

    rf_forward = clamp(
        64.0 +
        0.25 * traffic +
        rng.gauss(0.0, 4.5),
        45.0,
        95.0,
    )

    sample = {
        "shelter_temp_c": shelter_temp,
        "humidity_pct": humidity,
        "pa_temp_c": pa_temp,
        "pa_temp_trend_c_per_min": gauss(
            rng, 0.0, 0.22, -0.7, 0.7
        ),
        "pa_shelter_delta_c": (
            pa_temp - shelter_temp
        ),

        "vibration_rms_mps2": gauss(
            rng, 0.25, 0.12, 0.03, 0.70
        ),
        "vibration_std_mps2": gauss(
            rng, 0.10, 0.05, 0.01, 0.30
        ),
        "vibration_peak_to_peak_mps2": gauss(
            rng, 0.75, 0.28, 0.15, 1.70
        ),
        "vibration_dominant_hz": gauss(
            rng, 9.0, 4.0, 1.0, 22.0
        ),

        "dc_voltage_v": dc_voltage,
        "dc_current_a": clamp(
            dc_current, 6.0, 15.0
        ),
        "dc_power_w": 0.0,

        "battery_voltage_v": battery_voltage,
        "battery_soc_pct": battery_soc,
        "battery_soc_trend_pct_per_min": gauss(
            rng, -0.02, 0.08, -0.3, 0.2
        ),

        "rf_forward_w": rf_forward,
        "rf_reflection_ratio_pct": gauss(
            rng, 2.6, 1.0, 0.5, 5.5
        ),
        "rf_reflected_w": 0.0,
        "vswr": 0.0,
        "return_loss_db": 0.0,

        "latency_ms": gauss(
            rng, 45.0, 8.0, 20.0, 70.0
        ),
        "latency_jitter_ms": gauss(
            rng, 3.5, 1.8, 0.3, 9.0
        ),
        "packet_loss_pct": gauss(
            rng, 0.7, 0.45, 0.0, 2.0
        ),
        "rssi_dbm": gauss(
            rng, -57.0, 4.5, -68.0, -46.0
        ),
        "rssi_drop_db": gauss(
            rng, 0.8, 0.6, 0.0, 2.5
        ),

        "traffic_load_pct": traffic,

        "physical_link_up": 1,
        "upstream_reachable": 1,
        "grid_available": 1,
        "generator_running": 0,
        "fan_operational": 1,
        "rectifier_normal": 1,
        "radio_operational": 1,
    }

    sample["dc_power_w"] = (
        sample["dc_voltage_v"] *
        sample["dc_current_a"]
    )

    derive_rf(sample)
    return sample


def apply_local_fault(
    sample: dict,
    cause: str,
    rng: random.Random,
    severity: float,
) -> None:
    s = severity

    if cause == "COOLING_FAULT":
        if rng.random() < 0.65 + 0.30 * s:
            sample["fan_operational"] = 0

        sample["shelter_temp_c"] += (
            2.0 + 7.0 * s + rng.gauss(0.0, 1.0)
        )

        sample["pa_temp_c"] = max(
            sample["pa_temp_c"],
            58.0 + 36.0 * s + rng.gauss(0.0, 2.5),
        )

        sample["pa_temp_trend_c_per_min"] = max(
            sample["pa_temp_trend_c_per_min"],
            1.5 + 8.0 * s + rng.gauss(0.0, 0.8),
        )

        sample["dc_current_a"] += (
            0.5 + 1.0 * s
        )

    elif cause == "RADIO_FAULT":
        if rng.random() < 0.70 + 0.25 * s:
            sample["radio_operational"] = 0

        sample["rf_forward_w"] *= clamp(
            1.0 - (0.35 + 0.50 * s),
            0.08,
            0.65,
        )

        sample["traffic_load_pct"] -= (
            5.0 + 30.0 * s
        )

        sample["dc_current_a"] -= (
            0.8 + 1.8 * s
        )

    elif cause == "RECTIFIER_FAULT":
        if rng.random() < 0.70 + 0.25 * s:
            sample["rectifier_normal"] = 0

        sample["dc_voltage_v"] = min(
            sample["dc_voltage_v"],
            45.5 - 6.0 * s + rng.gauss(0.0, 0.7),
        )

        sample["battery_soc_pct"] -= (
            10.0 + 30.0 * s
        )

        sample["battery_soc_trend_pct_per_min"] = min(
            sample["battery_soc_trend_pct_per_min"],
            -0.8 - 3.0 * s,
        )

    elif cause == "RF_MISMATCH":
        sample["rf_reflection_ratio_pct"] = max(
            sample["rf_reflection_ratio_pct"],
            7.0 + 35.0 * s + rng.gauss(0.0, 2.0),
        )

        sample["rf_forward_w"] *= clamp(
            1.0 - 0.05 * s,
            0.75,
            1.0,
        )

    elif cause == "BATTERY_LOW":
        sample["battery_soc_pct"] = clamp(
            28.0 - 23.0 * s + rng.gauss(0.0, 3.0),
            2.0,
            30.0,
        )

        sample["battery_soc_trend_pct_per_min"] = min(
            sample["battery_soc_trend_pct_per_min"],
            -0.4 - 2.0 * s,
        )

        if rng.random() < 0.45 + 0.35 * s:
            sample["grid_available"] = 0

        sample["generator_running"] = int(
            sample["grid_available"] == 0 and
            rng.random() < 0.45
        )

    elif cause == "GRID_FAILURE":
        sample["grid_available"] = 0
        sample["generator_running"] = int(
            rng.random() < 0.55
        )

        sample["battery_soc_trend_pct_per_min"] = min(
            sample["battery_soc_trend_pct_per_min"],
            -0.3 - 1.5 * s,
        )

        if not sample["generator_running"]:
            sample["dc_voltage_v"] -= (
                0.5 + 3.0 * s
            )
            sample["battery_soc_pct"] -= (
                5.0 + 15.0 * s
            )

    elif cause == "MECHANICAL_VIBRATION":
        sample["vibration_rms_mps2"] = max(
            sample["vibration_rms_mps2"],
            1.1 + 3.7 * s + rng.gauss(0.0, 0.2),
        )

        sample["vibration_std_mps2"] = max(
            sample["vibration_std_mps2"],
            0.5 + 1.7 * s,
        )

        sample["vibration_peak_to_peak_mps2"] = max(
            sample["vibration_peak_to_peak_mps2"],
            3.0 + 8.0 * s,
        )

        sample["vibration_dominant_hz"] = clamp(
            8.0 + 22.0 * s + rng.gauss(0.0, 2.0),
            5.0,
            35.0,
        )

    elif cause == "TRAFFIC_OVERLOAD":
        sample["traffic_load_pct"] = clamp(
            76.0 + 23.0 * s + rng.gauss(0.0, 3.0),
            70.0,
            100.0,
        )

        sample["dc_current_a"] += (
            1.5 + 3.0 * s
        )

        sample["pa_temp_c"] += (
            4.0 + 12.0 * s
        )

        sample["pa_temp_trend_c_per_min"] = max(
            sample["pa_temp_trend_c_per_min"],
            0.5 + 2.0 * s,
        )


def apply_upstream_fault(
    sample: dict,
    cause: str,
    rng: random.Random,
    severity: float,
) -> None:
    s = severity

    if cause == "BACKHAUL_CONGESTION":
        sample["physical_link_up"] = 1
        sample["upstream_reachable"] = 1

        sample["latency_ms"] = max(
            sample["latency_ms"],
            95.0 + 250.0 * s + rng.gauss(0.0, 20.0),
        )

        sample["latency_jitter_ms"] = max(
            sample["latency_jitter_ms"],
            12.0 + 55.0 * s + rng.gauss(0.0, 5.0),
        )

        sample["packet_loss_pct"] = max(
            sample["packet_loss_pct"],
            1.5 + 13.0 * s + rng.gauss(0.0, 1.0),
        )

        sample["traffic_load_pct"] = max(
            sample["traffic_load_pct"],
            60.0 + 38.0 * s + rng.gauss(0.0, 5.0),
        )

    elif cause == "UPSTREAM_LINK_DEGRADATION":
        sample["physical_link_up"] = 1

        sample["upstream_reachable"] = int(
            rng.random() > 0.08 + 0.12 * s
        )

        sample["rssi_dbm"] = min(
            sample["rssi_dbm"],
            -68.0 - 19.0 * s + rng.gauss(0.0, 2.0),
        )

        sample["rssi_drop_db"] = max(
            sample["rssi_drop_db"],
            4.0 + 15.0 * s + rng.gauss(0.0, 1.0),
        )

        sample["latency_ms"] = max(
            sample["latency_ms"],
            70.0 + 160.0 * s + rng.gauss(0.0, 15.0),
        )

        sample["packet_loss_pct"] = max(
            sample["packet_loss_pct"],
            2.0 + 22.0 * s + rng.gauss(0.0, 2.0),
        )

        sample["latency_jitter_ms"] = max(
            sample["latency_jitter_ms"],
            8.0 + 30.0 * s,
        )

    elif cause == "UPSTREAM_LINK_FAILURE":
        sample["physical_link_up"] = int(
            not (
                rng.random() <
                0.75 + 0.20 * s
            )
        )

        sample["upstream_reachable"] = 0

        sample["latency_ms"] = max(
            sample["latency_ms"],
            350.0 + 700.0 * s + rng.gauss(0.0, 50.0),
        )

        sample["packet_loss_pct"] = max(
            sample["packet_loss_pct"],
            55.0 + 45.0 * s + rng.gauss(0.0, 3.0),
        )

        sample["rssi_dbm"] = min(
            sample["rssi_dbm"],
            -78.0 - 12.0 * s + rng.gauss(0.0, 2.0),
        )

        sample["rssi_drop_db"] = max(
            sample["rssi_drop_db"],
            10.0 + 15.0 * s,
        )

    elif cause == "UPSTREAM_OUTAGE":
        sample["physical_link_up"] = int(
            rng.random() < 0.85
        )

        sample["upstream_reachable"] = 0

        sample["latency_ms"] = max(
            sample["latency_ms"],
            450.0 + 650.0 * s + rng.gauss(0.0, 60.0),
        )

        sample["packet_loss_pct"] = max(
            sample["packet_loss_pct"],
            75.0 + 25.0 * s + rng.gauss(0.0, 2.0),
        )

        sample["latency_jitter_ms"] = max(
            sample["latency_jitter_ms"],
            25.0 + 45.0 * s,
        )

        if sample["physical_link_up"]:
            sample["rssi_dbm"] = clamp(
                sample["rssi_dbm"] + rng.gauss(0.0, 1.5),
                -68.0,
                -46.0,
            )


def finalize_sample(sample: dict) -> None:
    sample["traffic_load_pct"] = clamp(
        sample["traffic_load_pct"], 0.0, 100.0
    )

    sample["shelter_temp_c"] = clamp(
        sample["shelter_temp_c"], -5.0, 60.0
    )

    sample["humidity_pct"] = clamp(
        sample["humidity_pct"], 0.0, 100.0
    )

    sample["pa_temp_c"] = clamp(
        sample["pa_temp_c"], -5.0, 120.0
    )

    sample["pa_shelter_delta_c"] = (
        sample["pa_temp_c"] -
        sample["shelter_temp_c"]
    )

    sample["dc_voltage_v"] = clamp(
        sample["dc_voltage_v"], 35.0, 55.0
    )

    sample["dc_current_a"] = clamp(
        sample["dc_current_a"], 0.5, 25.0
    )

    sample["dc_power_w"] = (
        sample["dc_voltage_v"] *
        sample["dc_current_a"]
    )

    sample["battery_soc_pct"] = clamp(
        sample["battery_soc_pct"], 0.0, 100.0
    )

    sample["battery_voltage_v"] = clamp(
        11.38 +
        0.0104 * sample["battery_soc_pct"],
        10.4,
        12.6,
    )

    sample["rf_forward_w"] = clamp(
        sample["rf_forward_w"], 0.1, 120.0
    )

    derive_rf(sample)

    sample["latency_ms"] = clamp(
        sample["latency_ms"], 1.0, 1500.0
    )

    sample["latency_jitter_ms"] = clamp(
        sample["latency_jitter_ms"], 0.0, 500.0
    )

    sample["packet_loss_pct"] = clamp(
        sample["packet_loss_pct"], 0.0, 100.0
    )

    sample["rssi_dbm"] = clamp(
        sample["rssi_dbm"], -120.0, -30.0
    )

    sample["rssi_drop_db"] = clamp(
        sample["rssi_drop_db"], 0.0, 60.0
    )

    sample["vibration_rms_mps2"] = clamp(
        sample["vibration_rms_mps2"], 0.0, 10.0
    )

    sample["vibration_std_mps2"] = clamp(
        sample["vibration_std_mps2"], 0.0, 10.0
    )

    sample["vibration_peak_to_peak_mps2"] = clamp(
        sample["vibration_peak_to_peak_mps2"], 0.0, 25.0
    )

    for key, value in list(sample.items()):
        if isinstance(value, float):
            sample[key] = round(value, 4)


def make_assignments(
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
        domains[index % len(domains)]
        for index in range(sample_count)
    ]

    rng.shuffle(assignments)
    return assignments


def generate_dataset(
    sample_count: int,
    seed: int,
) -> list[dict]:
    if sample_count < 4:
        raise ValueError(
            "sample_count must be at least 4"
        )

    rng = random.Random(seed)

    assignments = make_assignments(
        sample_count,
        rng,
    )

    local_cycle = itertools.cycle(
        LOCAL_CAUSES
    )

    upstream_cycle = itertools.cycle(
        UPSTREAM_CAUSES
    )

    mixed_cycle = itertools.cycle(
        list(
            itertools.product(
                LOCAL_CAUSES,
                UPSTREAM_CAUSES,
            )
        )
    )

    rows = []

    for sample_id, domain in enumerate(
        assignments,
        start=1,
    ):
        sample = make_healthy_baseline(rng)

        local_cause = "NONE"
        upstream_cause = "NONE"

        if domain == "LOCAL":
            local_cause = next(local_cycle)

            apply_local_fault(
                sample,
                local_cause,
                rng,
                rng.uniform(0.35, 1.0),
            )

        elif domain == "UPSTREAM":
            upstream_cause = next(
                upstream_cycle
            )

            apply_upstream_fault(
                sample,
                upstream_cause,
                rng,
                rng.uniform(0.35, 1.0),
            )

        elif domain == "MIXED":
            (
                local_cause,
                upstream_cause,
            ) = next(mixed_cycle)

            apply_local_fault(
                sample,
                local_cause,
                rng,
                rng.uniform(0.35, 1.0),
            )

            apply_upstream_fault(
                sample,
                upstream_cause,
                rng,
                rng.uniform(0.35, 1.0),
            )

        finalize_sample(sample)

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

    return rows


def validate_no_leakage(
    rows: list[dict],
) -> list[str]:
    feature_columns = [
        column
        for column in rows[0].keys()
        if column not in {
            "sample_id",
            *TARGET_COLUMNS,
        }
    ]

    leakage = (
        set(feature_columns) &
        FORBIDDEN_LEAKAGE_COLUMNS
    )

    if leakage:
        raise RuntimeError(
            "Leakage columns found: " +
            ", ".join(sorted(leakage))
        )

    return feature_columns


def save_dataset(
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
            fieldnames=list(rows[0].keys()),
        )

        writer.writeheader()
        writer.writerows(rows)


def save_manifest(
    rows: list[dict],
    feature_columns: list[str],
    seed: int,
    output_path: Path,
) -> None:
    manifest = {
        "schema": "abs.ml.dataset.v1",
        "seed": seed,
        "samples": len(rows),
        "fault_domains": dict(
            Counter(
                row["fault_domain"]
                for row in rows
            )
        ),
        "local_root_causes": LOCAL_CAUSES,
        "upstream_root_causes": UPSTREAM_CAUSES,
        "target_columns": TARGET_COLUMNS,
        "feature_columns": feature_columns,
        "forbidden_leakage_columns": sorted(
            FORBIDDEN_LEAKAGE_COLUMNS
        ),
    }

    with output_path.open(
        "w",
        encoding="utf-8",
    ) as handle:
        json.dump(
            manifest,
            handle,
            indent=2,
        )


def main() -> None:
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--samples",
        type=int,
        default=12000,
    )

    parser.add_argument(
        "--seed",
        type=int,
        default=42,
    )

    parser.add_argument(
        "--output",
        type=Path,
        default=(
            Path(__file__).resolve().parent /
            "data" /
            "base_station_fault_dataset.csv"
        ),
    )

    args = parser.parse_args()

    rows = generate_dataset(
        args.samples,
        args.seed,
    )

    feature_columns = validate_no_leakage(
        rows
    )

    save_dataset(
        rows,
        args.output,
    )

    manifest_path = (
        args.output.parent /
        "dataset_manifest.json"
    )

    save_manifest(
        rows,
        feature_columns,
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
        "DATASET GENERATOR"
    )
    print("=" * 54)
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
        f"Dataset          : "
        f"{args.output.resolve()}"
    )
    print(
        f"Manifest         : "
        f"{manifest_path.resolve()}"
    )


if __name__ == "__main__":
    main()