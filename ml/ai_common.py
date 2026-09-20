#!/usr/bin/env python3
"""
Shared AI utilities for the Autonomous Base Station project.

All supervised models use the same:
- target labels
- leakage rules
- feature selection
- train/test split
- dataset fingerprint

This keeps Random Forest, XGBoost and deep-learning comparisons fair.
"""

from __future__ import annotations

import hashlib
from pathlib import Path

import pandas as pd
from sklearn.model_selection import train_test_split


LABEL_ORDER = [
    "NORMAL",
    "LOCAL",
    "UPSTREAM",
    "MIXED",
]

TARGET_COLUMNS = {
    "fault_domain",
    "root_cause",
    "local_root_cause",
    "upstream_root_cause",
}

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
    "ai_command_status",
    "ai_recommended_mode",
    "ai_recommendation_reason",
    "ai_fault_domain",
    "ai_domain_confidence",
    "ai_anomaly_flag",
    "ai_anomaly_score",
    "ai_command_age_ms",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()

    with path.open("rb") as handle:
        for chunk in iter(
            lambda: handle.read(1024 * 1024),
            b"",
        ):
            digest.update(chunk)

    return digest.hexdigest()


def load_dataset(path: Path) -> pd.DataFrame:
    if not path.exists():
        raise FileNotFoundError(
            f"Dataset not found: {path}"
        )

    df = pd.read_csv(path)

    if "fault_domain" not in df.columns:
        raise ValueError(
            "Dataset does not contain fault_domain."
        )

    unexpected = (
        set(df["fault_domain"].unique()) -
        set(LABEL_ORDER)
    )

    if unexpected:
        raise ValueError(
            "Unexpected fault-domain labels: " +
            ", ".join(sorted(unexpected))
        )

    return df


def build_feature_list(
    df: pd.DataFrame,
) -> list[str]:
    excluded = {
        "sample_id",
        *TARGET_COLUMNS,
    }

    features = [
        column
        for column in df.columns
        if column not in excluded
    ]

    leakage = (
        set(features) &
        FORBIDDEN_LEAKAGE_COLUMNS
    )

    if leakage:
        raise RuntimeError(
            "Leakage columns detected: " +
            ", ".join(sorted(leakage))
        )

    non_numeric = [
        column
        for column in features
        if not pd.api.types.is_numeric_dtype(
            df[column]
        )
    ]

    if non_numeric:
        raise RuntimeError(
            "Non-numeric feature columns detected: " +
            ", ".join(non_numeric)
        )

    return features


def split_fault_domain_dataset(
    df: pd.DataFrame,
    feature_columns: list[str],
    test_size: float,
    seed: int,
):
    x = df[feature_columns]
    y = df["fault_domain"]

    return train_test_split(
        x,
        y,
        test_size=test_size,
        random_state=seed,
        stratify=y,
    )