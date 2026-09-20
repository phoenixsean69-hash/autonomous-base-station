#!/usr/bin/env python3
"""
Temporal V3 dataset for the Autonomous Base Station project.

Purpose
-------
V1/V2 contain independent telemetry snapshots.
V3 adds time so temporal deep-learning models can learn HOW a fault develops.

Each sequence:
    24 timesteps x 33 telemetry features

Default cadence:
    5 seconds per timestep
    24 timesteps = 120 seconds of telemetry

Sequence-level target:
    NORMAL / LOCAL / UPSTREAM / MIXED

Design goals
------------
- preserve the same 33 telemetry features used by the snapshot models
- introduce gradual fault onset instead of instant textbook failures
- permit different LOCAL and UPSTREAM onset times in MIXED sequences
- recompute temporal trend features from actual sequence motion
- add benign transient disturbances
- keep rule-engine outputs excluded from model inputs
- save compact NumPy arrays for TCN / 1D-CNN training
"""

from __future__ import annotations

import argparse
import itertools
import json
import math
import random
from collections import Counter
from pathlib import Path

import numpy as np

import generate_dataset as v1
import generate_dataset_v2 as v2
from ai_common import FORBIDDEN_LEAKAGE_COLUMNS, LABEL_ORDER


STATUS_KEYS = [
    "physical_link_up",
    "upstream_reachable",
    "grid_available",
    "generator_running",
    "fan_operational",
    "rectifier_normal",
    "radio_operational",
]

LABEL_TO_ID = {
    label: index
    for index, label in enumerate(LABEL_ORDER)
}


def smoothstep(value: float) -> float:
    value = max(
        0.0,
        min(
            1.0,
            value,
        ),
    )

    return (
        value *
        value *
        (3.0 - 2.0 * value)
    )


def onset_progress(
    timestep: int,
    onset: int,
    ramp_steps: int,
) -> float:
    if timestep < onset:
        return 0.0

    denominator = max(
        1,
        ramp_steps,
    )

    raw = (
        (timestep - onset + 1) /
        denominator
    )

    return smoothstep(
        raw
    )


def make_feature_columns() -> list[str]:
    rng = random.Random(1)

    sample = v1.make_healthy_baseline(
        rng
    )

    v1.finalize_sample(
        sample
    )

    feature_columns = list(
        sample.keys()
    )

    leakage = (
        set(feature_columns) &
        FORBIDDEN_LEAKAGE_COLUMNS
    )

    if leakage:
        raise RuntimeError(
            "Leakage features detected: " +
            ", ".join(
                sorted(leakage)
            )
        )

    if len(feature_columns) != 33:
        raise RuntimeError(
            "Expected exactly 33 telemetry features, "
            f"found {len(feature_columns)}."
        )

    return feature_columns


def make_balanced_domains(
    sequence_count: int,
    rng: random.Random,
) -> list[str]:
    domains = [
        LABEL_ORDER[
            index % len(LABEL_ORDER)
        ]
        for index in range(
            sequence_count
        )
    ]

    rng.shuffle(
        domains
    )

    return domains


def numeric_delta(
    baseline: dict,
    target: dict,
    key: str,
) -> float:
    return (
        float(target[key]) -
        float(baseline[key])
    )


def build_state(
    baseline: dict,
    local_target: dict | None,
    upstream_target: dict | None,
    normal_target: dict | None,
    local_progress: float,
    upstream_progress: float,
    normal_progress: float,
    status_thresholds: dict[str, float],
) -> dict:
    sample = baseline.copy()

    for key, base_value in baseline.items():
        if key in STATUS_KEYS:
            value = int(
                round(
                    float(base_value)
                )
            )

            candidates = []

            if local_target is not None:
                candidates.append(
                    (
                        int(
                            round(
                                float(
                                    local_target[key]
                                )
                            )
                        ),
                        local_progress,
                    )
                )

            if upstream_target is not None:
                candidates.append(
                    (
                        int(
                            round(
                                float(
                                    upstream_target[key]
                                )
                            )
                        ),
                        upstream_progress,
                    )
                )

            if normal_target is not None:
                candidates.append(
                    (
                        int(
                            round(
                                float(
                                    normal_target[key]
                                )
                            )
                        ),
                        normal_progress,
                    )
                )

            for candidate, progress in candidates:
                if (
                    candidate != value and
                    progress >= status_thresholds[key]
                ):
                    value = candidate

            sample[key] = value
            continue

        value = float(
            base_value
        )

        if normal_target is not None:
            value += (
                numeric_delta(
                    baseline,
                    normal_target,
                    key,
                ) *
                normal_progress
            )

        if local_target is not None:
            value += (
                numeric_delta(
                    baseline,
                    local_target,
                    key,
                ) *
                local_progress
            )

        if upstream_target is not None:
            value += (
                numeric_delta(
                    baseline,
                    upstream_target,
                    key,
                ) *
                upstream_progress
            )

        sample[key] = value

    return sample


def add_slow_environment_drift(
    sample: dict,
    timestep: int,
    sequence_length: int,
    rng_offsets: dict,
) -> None:
    phase = (
        timestep /
        max(
            1,
            sequence_length - 1,
        )
    )

    sample["shelter_temp_c"] += (
        rng_offsets["shelter"] *
        phase
    )

    sample["humidity_pct"] += (
        rng_offsets["humidity"] *
        phase
    )

    sample["traffic_load_pct"] += (
        rng_offsets["traffic"] *
        math.sin(
            math.pi * phase
        )
    )


def add_temporal_transient(
    sample: dict,
    timestep: int,
    transient: dict | None,
) -> None:
    if transient is None:
        return

    width = transient["width"]
    center = transient["center"]

    distance = (
        (timestep - center) /
        max(
            0.75,
            width,
        )
    )

    pulse = math.exp(
        -0.5 *
        distance *
        distance
    )

    amplitude = transient["amplitude"]
    kind = transient["kind"]

    if kind == "LATENCY":
        sample["latency_ms"] += (
            amplitude *
            pulse
        )

        sample["latency_jitter_ms"] += (
            0.18 *
            amplitude *
            pulse
        )

    elif kind == "LOSS":
        sample["packet_loss_pct"] += (
            amplitude *
            pulse
        )

    elif kind == "RSSI":
        sample["rssi_dbm"] -= (
            amplitude *
            pulse
        )

    elif kind == "RF":
        sample["rf_reflection_ratio_pct"] += (
            amplitude *
            pulse
        )

    elif kind == "TEMP":
        sample["pa_temp_c"] += (
            amplitude *
            pulse
        )


def add_temporal_measurement_noise(
    sample: dict,
    rng: random.Random,
) -> None:
    for key, sigma in v2.PRIMARY_NOISE_SIGMA.items():
        # Smaller than V2 snapshot noise to preserve temporal continuity.
        sample[key] += rng.gauss(
            0.0,
            sigma * 0.32,
        )

    # Rare one-frame telemetry/status glitch.
    if rng.random() < 0.003:
        key = rng.choice(
            STATUS_KEYS
        )

        sample[key] = (
            0
            if int(sample[key]) == 1
            else 1
        )


def choose_transient(
    rng: random.Random,
) -> dict | None:
    if rng.random() >= 0.30:
        return None

    kind = rng.choice(
        [
            "LATENCY",
            "LOSS",
            "RSSI",
            "RF",
            "TEMP",
        ]
    )

    amplitude_ranges = {
        "LATENCY": (
            18.0,
            85.0,
        ),
        "LOSS": (
            1.0,
            6.0,
        ),
        "RSSI": (
            1.0,
            7.0,
        ),
        "RF": (
            1.5,
            7.5,
        ),
        "TEMP": (
            1.5,
            8.0,
        ),
    }

    low, high = (
        amplitude_ranges[kind]
    )

    return {
        "kind": kind,
        "center": rng.uniform(
            4.0,
            19.0,
        ),
        "width": rng.uniform(
            0.8,
            2.2,
        ),
        "amplitude": rng.uniform(
            low,
            high,
        ),
    }


def recompute_temporal_features(
    sample: dict,
    previous: dict | None,
    baseline_rssi: float,
    sample_interval_seconds: float,
) -> None:
    if previous is None:
        sample["pa_temp_trend_c_per_min"] *= 0.35
        sample["battery_soc_trend_pct_per_min"] *= 0.35
    else:
        scale = (
            60.0 /
            sample_interval_seconds
        )

        sample[
            "pa_temp_trend_c_per_min"
        ] = (
            (
                sample["pa_temp_c"] -
                previous["pa_temp_c"]
            ) *
            scale
        )

        sample[
            "battery_soc_trend_pct_per_min"
        ] = (
            (
                sample["battery_soc_pct"] -
                previous["battery_soc_pct"]
            ) *
            scale
        )

    sample["rssi_drop_db"] = max(
        0.0,
        baseline_rssi -
        sample["rssi_dbm"],
    )


def make_upstream_temporal_target(
    baseline: dict,
    upstream_cause: str,
    rng: random.Random,
    counters: Counter,
) -> dict:
    """
    Generate upstream targets including remote/core outages where the local
    physical link remains healthy.

    Important case:
        physical_link_up = 1
        upstream_reachable = 0

    Latency, packet loss and RSSI may remain nominal because the failure can
    exist beyond the locally measured backhaul segment.
    """
    if upstream_cause == "UPSTREAM_OUTAGE":
        profile_roll = rng.random()

        # Route/core/reachability-only outage.
        if profile_roll < 0.80:
            target = baseline.copy()

            target["physical_link_up"] = 1
            target["upstream_reachable"] = 0

            target["latency_ms"] = v1.clamp(
                baseline["latency_ms"] + rng.uniform(-4.0, 12.0),
                20.0,
                90.0,
            )

            target["latency_jitter_ms"] = v1.clamp(
                baseline["latency_jitter_ms"] + rng.uniform(-1.0, 2.5),
                0.2,
                12.0,
            )

            target["packet_loss_pct"] = v1.clamp(
                baseline["packet_loss_pct"] + rng.uniform(-0.2, 1.0),
                0.0,
                3.5,
            )

            target["rssi_dbm"] = v1.clamp(
                baseline["rssi_dbm"] + rng.uniform(-2.0, 2.0),
                -68.0,
                -46.0,
            )

            target["rssi_drop_db"] = v1.clamp(
                baseline["rssi_drop_db"] + rng.uniform(-0.5, 1.0),
                0.0,
                3.5,
            )

            counters[
                "upstream_profile:REACHABILITY_ONLY_OUTAGE"
            ] += 1

            return target

        # Mild route/core outage.
        if profile_roll < 0.93:
            target = baseline.copy()

            target["physical_link_up"] = 1
            target["upstream_reachable"] = 0

            target["latency_ms"] = max(
                baseline["latency_ms"],
                rng.uniform(55.0, 110.0),
            )

            target["latency_jitter_ms"] = max(
                baseline["latency_jitter_ms"],
                rng.uniform(2.0, 12.0),
            )

            target["packet_loss_pct"] = max(
                baseline["packet_loss_pct"],
                rng.uniform(0.5, 4.0),
            )

            target["rssi_dbm"] = v1.clamp(
                baseline["rssi_dbm"] + rng.uniform(-2.5, 1.5),
                -72.0,
                -44.0,
            )

            target["rssi_drop_db"] = max(
                baseline["rssi_drop_db"],
                rng.uniform(0.0, 4.0),
            )

            counters[
                "upstream_profile:ROUTE_CORE_OUTAGE"
            ] += 1

            return target

    counters[
        "upstream_profile:PHYSICAL_OR_DEGRADED"
    ] += 1

    return v2.make_fault_sample(
        baseline,
        "UPSTREAM",
        "NONE",
        upstream_cause,
        rng,
        counters,
    )

def build_sequence(
    domain: str,
    local_cause: str,
    upstream_cause: str,
    sequence_length: int,
    sample_interval_seconds: float,
    rng: random.Random,
    counters: Counter,
    feature_columns: list[str],
) -> np.ndarray:
    baseline = v1.make_healthy_baseline(
        rng
    )

    local_target = None
    upstream_target = None
    normal_target = None

    if domain == "NORMAL":
        normal_target = baseline.copy()

        v2.apply_normal_stress(
            normal_target,
            rng,
            counters,
        )

        normal_ramp = rng.randint(
            8,
            16,
        )

        normal_onset = rng.randint(
            0,
            4,
        )

    else:
        normal_ramp = 1
        normal_onset = sequence_length + 1

    if domain in {
        "LOCAL",
        "MIXED",
    }:
        local_target = v2.make_fault_sample(
            baseline,
            "LOCAL",
            local_cause,
            "NONE",
            rng,
            counters,
        )

        local_onset = rng.randint(
            4,
            11,
        )

        local_ramp = rng.randint(
            5,
            10,
        )

    else:
        local_onset = sequence_length + 1
        local_ramp = 1

    if domain in {
        "UPSTREAM",
        "MIXED",
    }:
        upstream_target = make_upstream_temporal_target(
            baseline,
            upstream_cause,
            rng,
            counters,
        )

        upstream_onset = rng.randint(
            3,
            10,
        )

        upstream_ramp = rng.randint(
            4,
            9,
        )

    else:
        upstream_onset = sequence_length + 1
        upstream_ramp = 1

    if domain == "MIXED":
        # Avoid making every mixed sequence perfectly simultaneous.
        if rng.random() < 0.72:
            offset = rng.choice(
                [
                    -4,
                    -3,
                    -2,
                    2,
                    3,
                    4,
                ]
            )

            if rng.random() < 0.5:
                local_onset = max(
                    2,
                    min(
                        sequence_length - 7,
                        upstream_onset + offset,
                    ),
                )
            else:
                upstream_onset = max(
                    2,
                    min(
                        sequence_length - 7,
                        local_onset + offset,
                    ),
                )

    status_thresholds = {
        key: rng.uniform(
            0.58,
            0.92,
        )
        for key in STATUS_KEYS
    }

    transient = choose_transient(
        rng
    )

    if transient is not None:
        counters[
            f"transient:{transient['kind']}"
        ] += 1

    environment_drift = {
        "shelter": rng.uniform(
            -1.2,
            2.0,
        ),
        "humidity": rng.uniform(
            -3.5,
            3.5,
        ),
        "traffic": rng.uniform(
            -8.0,
            15.0,
        ),
    }

    sequence = np.zeros(
        (
            sequence_length,
            len(feature_columns),
        ),
        dtype=np.float32,
    )

    previous = None

    for timestep in range(
        sequence_length
    ):
        normal_progress = onset_progress(
            timestep,
            normal_onset,
            normal_ramp,
        )

        local_progress = onset_progress(
            timestep,
            local_onset,
            local_ramp,
        )

        upstream_progress = onset_progress(
            timestep,
            upstream_onset,
            upstream_ramp,
        )

        sample = build_state(
            baseline,
            local_target,
            upstream_target,
            normal_target,
            local_progress,
            upstream_progress,
            normal_progress,
            status_thresholds,
        )

        add_slow_environment_drift(
            sample,
            timestep,
            sequence_length,
            environment_drift,
        )

        add_temporal_transient(
            sample,
            timestep,
            transient,
        )

        add_temporal_measurement_noise(
            sample,
            rng,
        )

        recompute_temporal_features(
            sample,
            previous,
            baseline["rssi_dbm"],
            sample_interval_seconds,
        )

        v1.finalize_sample(
            sample
        )

        sequence[
            timestep
        ] = np.asarray(
            [
                float(sample[key])
                for key in feature_columns
            ],
            dtype=np.float32,
        )

        previous = sample.copy()

    # Diagnostics: final part of a fault sequence should normally contain
    # non-zero fault progress while NORMAL remains a healthy/stress sequence.
    if domain != "NORMAL":
        counters[
            f"fault_sequence:{domain}"
        ] += 1

    return sequence


def generate_dataset(
    sequence_count: int,
    sequence_length: int,
    sample_interval_seconds: float,
    seed: int,
):
    if sequence_count < 4:
        raise ValueError(
            "sequence_count must be at least 4"
        )

    if sequence_length < 12:
        raise ValueError(
            "sequence_length must be at least 12"
        )

    rng = random.Random(
        seed
    )

    feature_columns = make_feature_columns()

    assignments = make_balanced_domains(
        sequence_count,
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

    x = np.zeros(
        (
            sequence_count,
            sequence_length,
            len(feature_columns),
        ),
        dtype=np.float32,
    )

    y = np.zeros(
        sequence_count,
        dtype=np.int64,
    )

    domains = []
    local_causes = []
    upstream_causes = []

    counters = Counter()

    for index, domain in enumerate(
        assignments
    ):
        local_cause = "NONE"
        upstream_cause = "NONE"

        if domain == "LOCAL":
            local_cause = next(
                local_cycle
            )

        elif domain == "UPSTREAM":
            upstream_cause = next(
                upstream_cycle
            )

        elif domain == "MIXED":
            (
                local_cause,
                upstream_cause,
            ) = next(
                mixed_cycle
            )

        x[index] = build_sequence(
            domain,
            local_cause,
            upstream_cause,
            sequence_length,
            sample_interval_seconds,
            rng,
            counters,
            feature_columns,
        )

        y[index] = LABEL_TO_ID[
            domain
        ]

        domains.append(
            domain
        )

        local_causes.append(
            local_cause
        )

        upstream_causes.append(
            upstream_cause
        )

        counters[
            f"domain:{domain}"
        ] += 1

    return (
        x,
        y,
        np.asarray(
            domains,
            dtype="U16",
        ),
        np.asarray(
            local_causes,
            dtype="U32",
        ),
        np.asarray(
            upstream_causes,
            dtype="U32",
        ),
        feature_columns,
        counters,
    )


def main() -> None:
    here = Path(__file__).resolve().parent

    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--sequences",
        type=int,
        default=12000,
    )

    parser.add_argument(
        "--timesteps",
        type=int,
        default=24,
    )

    parser.add_argument(
        "--sample-interval-seconds",
        type=float,
        default=5.0,
    )

    parser.add_argument(
        "--seed",
        type=int,
        default=126,
    )

    parser.add_argument(
        "--output",
        type=Path,
        default=(
            here /
            "data" /
            "base_station_temporal_dataset_v3.npz"
        ),
    )

    parser.add_argument(
        "--manifest",
        type=Path,
        default=(
            here /
            "data" /
            "dataset_v3_manifest.json"
        ),
    )

    args = parser.parse_args()

    (
        x,
        y,
        domains,
        local_causes,
        upstream_causes,
        feature_columns,
        counters,
    ) = generate_dataset(
        args.sequences,
        args.timesteps,
        args.sample_interval_seconds,
        args.seed,
    )

    if not np.isfinite(x).all():
        raise RuntimeError(
            "Dataset contains NaN or infinite values."
        )

    args.output.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    np.savez_compressed(
        args.output,
        x=x,
        y=y,
        fault_domain=domains,
        local_root_cause=local_causes,
        upstream_root_cause=upstream_causes,
        feature_names=np.asarray(
            feature_columns,
            dtype="U64",
        ),
        label_order=np.asarray(
            LABEL_ORDER,
            dtype="U16",
        ),
        timestep_seconds=np.arange(
            args.timesteps,
            dtype=np.float32,
        ) * float(
            args.sample_interval_seconds
        ),
    )

    counts = Counter(
        domains.tolist()
    )

    manifest = {
        "schema": "abs.ml.dataset.v3.temporal",
        "purpose": (
            "temporal fault-domain dataset for "
            "1D-CNN / TCN deep-learning models"
        ),
        "seed": args.seed,
        "sequences": int(
            args.sequences
        ),
        "timesteps": int(
            args.timesteps
        ),
        "sample_interval_seconds": float(
            args.sample_interval_seconds
        ),
        "window_seconds": float(
            args.timesteps *
            args.sample_interval_seconds
        ),
        "feature_count": len(
            feature_columns
        ),
        "feature_columns": (
            feature_columns
        ),
        "label_order": LABEL_ORDER,
        "fault_domains": dict(
            counts
        ),
        "difficulty_mechanisms": [
            "gradual_fault_onset",
            "independent_mixed_fault_onsets",
            "healthy_stress_sequences",
            "benign_temporal_transients",
            "status_bit_lag",
            "measurement_noise",
            "slow_environment_drift",
            "recomputed_temporal_trends",
            "reachability_only_upstream_outages",
        ],
        "diagnostics": dict(
            counters
        ),
    }

    args.manifest.write_text(
        json.dumps(
            manifest,
            indent=2,
        ),
        encoding="utf-8",
    )

    print()
    print(
        "AUTONOMOUS BASE STATION - "
        "TEMPORAL DATASET V3"
    )
    print("=" * 64)
    print(
        f"Sequences         : {x.shape[0]}"
    )
    print(
        f"Timesteps         : {x.shape[1]}"
    )
    print(
        f"Feature columns   : {x.shape[2]}"
    )
    print(
        f"Window            : "
        f"{args.timesteps * args.sample_interval_seconds:.0f} seconds"
    )
    print(
        f"Sample interval   : "
        f"{args.sample_interval_seconds:.1f} seconds"
    )
    print(
        "Leakage guard     : PASS"
    )
    print(
        "Finite-value check: PASS"
    )

    print()
    print("[ FAULT DOMAINS ]")

    for label in LABEL_ORDER:
        print(
            f"{label:<12}: "
            f"{counts[label]}"
        )

    print()
    print("[ TEMPORAL DIFFICULTY ]")
    print(
        "Gradual onset     : YES"
    )
    print(
        "Mixed async onset : YES"
    )
    print(
        "Benign transients : "
        f"{sum(value for key, value in counters.items() if key.startswith('transient:'))}"
    )
    print(
        "Trend recompute   : YES"
    )

    print()
    print(
        f"Dataset           : "
        f"{args.output.resolve()}"
    )
    print(
        f"Manifest          : "
        f"{args.manifest.resolve()}"
    )

    print()
    print(
        "Next stage        : train a temporal "
        "1D-CNN / TCN on this sequence dataset."
    )


if __name__ == "__main__":
    main()