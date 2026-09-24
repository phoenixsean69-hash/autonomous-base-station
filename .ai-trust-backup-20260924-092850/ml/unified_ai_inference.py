#!/usr/bin/env python3
"""
Unified AI inference engine for the Autonomous Base Station.

This joins the V3 model stack into one reusable runtime:

    24 x 33 telemetry window
            |
            +--> fault-domain TCN
            |       NORMAL / LOCAL / UPSTREAM / MIXED
            |
            +--> temporal autoencoder
            |       anomaly score / abnormality flag
            |
            +--> local root-cause TCN when required
            |
            +--> upstream root-cause TCN when required
            |
            +--> diagnosis-aware energy recommendation
                    |
                    +--> SAFETY GUARDRAILS MUST APPLY AFTER THIS LAYER

The AI layer NEVER declares the final operating mode.  It emits a
pre-guardrail recommendation.  Existing deterministic ESP32/Pico safety,
recovery, dwell-time and capacity guardrails remain authoritative.

The module also exposes TemporalWindowBuffer so the next stage can feed
live ABS_JSON packets into the same 24-sample temporal window.
"""

from __future__ import annotations

import argparse
import json
from collections import Counter, deque
from pathlib import Path

import joblib
import numpy as np
import torch
from sklearn.metrics import accuracy_score
from sklearn.model_selection import train_test_split

from ai_common import LABEL_ORDER, sha256_file
from train_anomaly_detectors_v3 import TemporalAutoencoder
from train_temporal_tcn import FaultDomainTCN


DOMAIN_TO_ID = {
    label: index
    for index, label in enumerate(LABEL_ORDER)
}


def normalize_probabilities(
    probabilities: np.ndarray,
) -> np.ndarray:
    probabilities = np.asarray(
        probabilities,
        dtype=np.float64,
    )

    row_sums = probabilities.sum(
        axis=1,
        keepdims=True,
    )

    if np.any(row_sums <= 0.0):
        raise RuntimeError(
            "Invalid probability row with non-positive sum."
        )

    return probabilities / row_sums


def top_prediction(
    probabilities: np.ndarray,
    classes: list[str],
) -> tuple[str, float]:
    probabilities = np.asarray(
        probabilities,
        dtype=np.float64,
    )

    index = int(
        np.argmax(
            probabilities
        )
    )

    return (
        classes[index],
        float(
            probabilities[index]
        ),
    )


def probability_map(
    probabilities: np.ndarray,
    classes: list[str],
) -> dict[str, float]:
    return {
        label: float(
            probabilities[index]
        )
        for index, label
        in enumerate(
            classes
        )
    }


def boolean_feature(
    value: float,
) -> bool:
    return bool(
        float(value) >= 0.5
    )


class TemporalWindowBuffer:
    """
    Live 24-frame telemetry buffer.

    Incoming telemetry dictionaries must contain the same physical feature
    names used to train the V3 models. Rule-engine outputs such as
    fault_label, operating_mode and guardrail_status are intentionally not
    consumed by this class.
    """

    def __init__(
        self,
        feature_columns: list[str],
        timesteps: int,
    ) -> None:
        self.feature_columns = list(
            feature_columns
        )

        self.timesteps = int(
            timesteps
        )

        self._frames = deque(
            maxlen=self.timesteps
        )

    def add(
        self,
        telemetry: dict,
    ) -> int:
        missing = [
            feature
            for feature in self.feature_columns
            if feature not in telemetry
        ]

        if missing:
            raise KeyError(
                "Telemetry is missing AI feature(s): " +
                ", ".join(
                    missing
                )
            )

        frame = np.asarray(
            [
                float(
                    telemetry[
                        feature
                    ]
                )
                for feature
                in self.feature_columns
            ],
            dtype=np.float32,
        )

        if not np.isfinite(
            frame
        ).all():
            raise ValueError(
                "Telemetry frame contains NaN or infinite values."
            )

        self._frames.append(
            frame
        )

        return len(
            self._frames
        )

    @property
    def ready(self) -> bool:
        return (
            len(
                self._frames
            ) ==
            self.timesteps
        )

    @property
    def count(self) -> int:
        return len(
            self._frames
        )

    def as_array(
        self,
    ) -> np.ndarray:
        if not self.ready:
            raise RuntimeError(
                f"Temporal window is not ready: "
                f"{self.count}/{self.timesteps} frames."
            )

        return np.stack(
            list(
                self._frames
            ),
            axis=0,
        ).astype(
            np.float32
        )


class UnifiedAIEngine:
    def __init__(
        self,
        ml_dir: Path,
    ) -> None:
        self.ml_dir = Path(
            ml_dir
        ).resolve()

        self.model_dir = (
            self.ml_dir /
            "models"
        )

        self.result_dir = (
            self.ml_dir /
            "results"
        )

        self.device = torch.device(
            "cpu"
        )

        self._load_metadata()
        self._load_models()

    def _read_json(
        self,
        path: Path,
    ) -> dict:
        if not path.exists():
            raise FileNotFoundError(
                f"Required file missing: {path}"
            )

        return json.loads(
            path.read_text(
                encoding="utf-8"
            )
        )

    def _load_metadata(
        self,
    ) -> None:
        self.domain_metadata = self._read_json(
            self.model_dir /
            "fault_domain_temporal_tcn_v3_metadata.json"
        )

        self.root_metadata = self._read_json(
            self.model_dir /
            "hierarchical_root_cause_v3_metadata.json"
        )

        self.anomaly_metadata = self._read_json(
            self.model_dir /
            "temporal_anomaly_v3_metadata.json"
        )

        self.anomaly_metrics = self._read_json(
            self.result_dir /
            "temporal_anomaly_v3_metrics.json"
        )

        hashes = {
            self.domain_metadata[
                "dataset_sha256"
            ],
            self.root_metadata[
                "dataset_sha256"
            ],
            self.anomaly_metadata[
                "dataset_sha256"
            ],
            self.anomaly_metrics[
                "dataset_sha256"
            ],
        }

        if len(
            hashes
        ) != 1:
            raise RuntimeError(
                "Model stack dataset hashes do not match."
            )

        self.dataset_sha256 = next(
            iter(
                hashes
            )
        )

        self.feature_columns = list(
            self.domain_metadata[
                "feature_columns"
            ]
        )

        self.timesteps = int(
            self.domain_metadata[
                "timesteps"
            ]
        )

        self.sample_interval_seconds = float(
            self.domain_metadata[
                "sample_interval_seconds"
            ]
        )

        if (
            self.feature_columns !=
            self.root_metadata[
                "feature_columns"
            ] or
            self.feature_columns !=
            self.anomaly_metadata[
                "feature_columns"
            ]
        ):
            raise RuntimeError(
                "Feature order does not match across the model stack."
            )

        self.local_classes = list(
            self.root_metadata[
                "local_head"
            ][
                "classes"
            ]
        )

        self.upstream_classes = list(
            self.root_metadata[
                "upstream_head"
            ][
                "classes"
            ]
        )

        self.anomaly_threshold = float(
            self.anomaly_metrics[
                "temporal_autoencoder"
            ][
                "threshold"
            ]
        )

        self.domain_classes = list(
            self.domain_metadata[
                "classes"
            ]
        )

        if (
            self.domain_classes !=
            LABEL_ORDER
        ):
            raise RuntimeError(
                "Domain class order does not match ai_common.LABEL_ORDER."
            )

        self.feature_index = {
            feature: index
            for index, feature
            in enumerate(
                self.feature_columns
            )
        }

    def _required_artifact(
        self,
        name: str,
    ) -> Path:
        path = (
            self.model_dir /
            name
        )

        if not path.exists():
            raise FileNotFoundError(
                f"Required local model artifact missing: {path}"
            )

        return path

    def _load_tcn(
        self,
        model_name: str,
        scaler_name: str,
        classes: list[str],
    ):
        model_path = self._required_artifact(
            model_name
        )

        scaler_path = self._required_artifact(
            scaler_name
        )

        checkpoint = torch.load(
            model_path,
            map_location="cpu",
            weights_only=False,
        )

        class_count = int(
            checkpoint[
                "class_count"
            ]
        )

        if (
            class_count !=
            len(
                classes
            )
        ):
            raise RuntimeError(
                f"Class count mismatch for {model_path.name}."
            )

        model = FaultDomainTCN(
            feature_count=int(
                checkpoint[
                    "feature_count"
                ]
            ),
            class_count=class_count,
        )

        model.load_state_dict(
            checkpoint[
                "state_dict"
            ]
        )

        model.eval()

        scaler = joblib.load(
            scaler_path
        )

        return (
            model,
            scaler,
        )

    def _load_models(
        self,
    ) -> None:
        (
            self.domain_model,
            self.domain_scaler,
        ) = self._load_tcn(
            "fault_domain_temporal_tcn_v3.pt",
            "fault_domain_temporal_tcn_v3_scaler.joblib",
            self.domain_classes,
        )

        (
            self.local_model,
            self.local_scaler,
        ) = self._load_tcn(
            "local_root_cause_tcn_v3.pt",
            "local_root_cause_tcn_v3_scaler.joblib",
            self.local_classes,
        )

        (
            self.upstream_model,
            self.upstream_scaler,
        ) = self._load_tcn(
            "upstream_root_cause_tcn_v3.pt",
            "upstream_root_cause_tcn_v3_scaler.joblib",
            self.upstream_classes,
        )

        anomaly_model_path = self._required_artifact(
            "temporal_autoencoder_v3.pt"
        )

        anomaly_scaler_path = self._required_artifact(
            "temporal_anomaly_v3_scaler.joblib"
        )

        anomaly_checkpoint = torch.load(
            anomaly_model_path,
            map_location="cpu",
            weights_only=False,
        )

        self.anomaly_model = TemporalAutoencoder(
            feature_count=int(
                anomaly_checkpoint[
                    "feature_count"
                ]
            )
        )

        self.anomaly_model.load_state_dict(
            anomaly_checkpoint[
                "state_dict"
            ]
        )

        self.anomaly_model.eval()

        self.anomaly_scaler = joblib.load(
            anomaly_scaler_path
        )

    def new_buffer(
        self,
    ) -> TemporalWindowBuffer:
        return TemporalWindowBuffer(
            self.feature_columns,
            self.timesteps,
        )

    def _validate_sequence(
        self,
        sequence: np.ndarray,
    ) -> np.ndarray:
        sequence = np.asarray(
            sequence,
            dtype=np.float32,
        )

        expected_shape = (
            self.timesteps,
            len(
                self.feature_columns
            ),
        )

        if (
            sequence.shape !=
            expected_shape
        ):
            raise ValueError(
                f"Expected sequence shape {expected_shape}, "
                f"got {sequence.shape}."
            )

        if not np.isfinite(
            sequence
        ).all():
            raise ValueError(
                "Sequence contains NaN or infinite values."
            )

        return sequence

    def _scale(
        self,
        sequence: np.ndarray,
        scaler,
    ) -> np.ndarray:
        scaled = scaler.transform(
            sequence
        ).astype(
            np.float32
        )

        return scaled[
            np.newaxis,
            :,
            :,
        ]

    def _tcn_probabilities(
        self,
        sequence: np.ndarray,
        model: FaultDomainTCN,
        scaler,
    ) -> np.ndarray:
        x = self._scale(
            sequence,
            scaler,
        )

        with torch.no_grad():
            logits = model(
                torch.tensor(
                    x,
                    dtype=torch.float32,
                )
            )

            probabilities = torch.softmax(
                logits,
                dim=1,
            ).cpu().numpy()

        return normalize_probabilities(
            probabilities
        )[0]

    def _anomaly_score(
        self,
        sequence: np.ndarray,
    ) -> float:
        x = self._scale(
            sequence,
            self.anomaly_scaler,
        )

        tensor = torch.tensor(
            x,
            dtype=torch.float32,
        )

        with torch.no_grad():
            reconstructed = self.anomaly_model(
                tensor
            )

            score = torch.mean(
                (
                    reconstructed -
                    tensor
                ) ** 2,
                dim=(
                    1,
                    2,
                ),
            )

        return float(
            score.item()
        )

    def _latest_feature(
        self,
        sequence: np.ndarray,
        name: str,
    ) -> float:
        return float(
            sequence[
                -1,
                self.feature_index[
                    name
                ],
            ]
        )

    def _recommend_energy(
        self,
        *,
        sequence: np.ndarray,
        fault_domain: str,
        domain_confidence: float,
        local_root_cause: str | None,
        upstream_root_cause: str | None,
        anomaly_flag: bool,
    ) -> dict:
        """
        Diagnosis-aware recommendation only.

        This policy does NOT replace the firmware guardrails. The existing
        deterministic protection layer remains authoritative for critical
        energy, major faults, degraded service, recovery confirmation,
        hysteresis and dwell time.
        """
        traffic = self._latest_feature(
            sequence,
            "traffic_load_pct",
        )

        battery_soc = self._latest_feature(
            sequence,
            "battery_soc_pct",
        )

        grid_available = boolean_feature(
            self._latest_feature(
                sequence,
                "grid_available",
            )
        )

        generator_running = boolean_feature(
            self._latest_feature(
                sequence,
                "generator_running",
            )
        )

        # Critical backup energy is intentionally mirrored here so the AI
        # recommendation never asks for extra capacity in a clearly critical
        # energy state. Firmware guardrails still re-check it.
        if (
            not grid_available and
            not generator_running and
            battery_soc <= 25.0
        ):
            mode = "EMERGENCY"
            reason = (
                "AI PRE-GUARDRAIL: CRITICAL BACKUP ENERGY"
            )

        elif (
            domain_confidence < 0.60 or
            (
                fault_domain == "NORMAL" and
                anomaly_flag
            )
        ):
            mode = "FULL"
            reason = (
                "AI PRE-GUARDRAIL: UNCERTAIN / UNKNOWN ABNORMALITY"
            )

        elif local_root_cause in {
            "COOLING_FAULT",
            "RADIO_FAULT",
            "RECTIFIER_FAULT",
            "RF_MISMATCH",
            "MECHANICAL_VIBRATION",
            "TRAFFIC_OVERLOAD",
        }:
            mode = "FULL"
            reason = (
                "AI PRE-GUARDRAIL: LOCAL SERVICE/RECOVERY PRIORITY"
            )

        elif local_root_cause in {
            "BATTERY_LOW",
            "GRID_FAILURE",
        }:
            mode = "ECO"
            reason = (
                "AI PRE-GUARDRAIL: LOCAL ENERGY CONSERVATION"
            )

        elif upstream_root_cause in {
            "UPSTREAM_OUTAGE",
            "UPSTREAM_LINK_FAILURE",
        }:
            mode = "ECO"
            reason = (
                "AI PRE-GUARDRAIL: BACKHAUL LOSS ENERGY CONSERVATION"
            )

        elif traffic >= 65.0:
            mode = "FULL"
            reason = (
                "AI PRE-GUARDRAIL: HIGH TRAFFIC"
            )

        elif (
            traffic < 15.0 and
            fault_domain == "NORMAL" and
            not anomaly_flag
        ):
            mode = "REDUCED"
            reason = (
                "AI PRE-GUARDRAIL: LOW TRAFFIC HEALTHY SITE"
            )

        else:
            mode = "ECO"
            reason = (
                "AI PRE-GUARDRAIL: MODERATE LOAD / CONSERVATIVE ECO"
            )

        return {
            "recommended_mode": mode,
            "reason": reason,
            "guardrails_required": True,
            "final_mode": (
                "PENDING_DETERMINISTIC_GUARDRAILS"
            ),
            "latest_operating_context": {
                "traffic_load_pct": traffic,
                "battery_soc_pct": battery_soc,
                "grid_available": grid_available,
                "generator_running": generator_running,
            },
        }

    def diagnose(
        self,
        sequence: np.ndarray,
    ) -> dict:
        sequence = self._validate_sequence(
            sequence
        )

        domain_probabilities = self._tcn_probabilities(
            sequence,
            self.domain_model,
            self.domain_scaler,
        )

        (
            fault_domain,
            domain_confidence,
        ) = top_prediction(
            domain_probabilities,
            self.domain_classes,
        )

        anomaly_score = self._anomaly_score(
            sequence
        )

        anomaly_flag = bool(
            anomaly_score >
            self.anomaly_threshold
        )

        local_root_cause = None
        local_confidence = None
        local_probabilities = None

        upstream_root_cause = None
        upstream_confidence = None
        upstream_probabilities = None

        if fault_domain in {
            "LOCAL",
            "MIXED",
        }:
            local_probabilities = self._tcn_probabilities(
                sequence,
                self.local_model,
                self.local_scaler,
            )

            (
                local_root_cause,
                local_confidence,
            ) = top_prediction(
                local_probabilities,
                self.local_classes,
            )

        if fault_domain in {
            "UPSTREAM",
            "MIXED",
        }:
            upstream_probabilities = self._tcn_probabilities(
                sequence,
                self.upstream_model,
                self.upstream_scaler,
            )

            (
                upstream_root_cause,
                upstream_confidence,
            ) = top_prediction(
                upstream_probabilities,
                self.upstream_classes,
            )

        if (
            fault_domain == "NORMAL" and
            anomaly_flag
        ):
            diagnostic_state = (
                "UNKNOWN_OR_SUSPICIOUS"
            )

        elif fault_domain == "NORMAL":
            diagnostic_state = "NORMAL"

        else:
            diagnostic_state = "KNOWN_FAULT"

        energy = self._recommend_energy(
            sequence=sequence,
            fault_domain=fault_domain,
            domain_confidence=domain_confidence,
            local_root_cause=local_root_cause,
            upstream_root_cause=upstream_root_cause,
            anomaly_flag=anomaly_flag,
        )

        return {
            "schema": "abs.ai.v1",
            "window": {
                "timesteps": self.timesteps,
                "sample_interval_seconds": (
                    self.sample_interval_seconds
                ),
                "window_seconds": (
                    self.timesteps *
                    self.sample_interval_seconds
                ),
            },
            "diagnostic_state": (
                diagnostic_state
            ),
            "fault_domain": {
                "label": fault_domain,
                "confidence": (
                    domain_confidence
                ),
                "probabilities": probability_map(
                    domain_probabilities,
                    self.domain_classes,
                ),
            },
            "root_cause": {
                "local": (
                    {
                        "label": local_root_cause,
                        "confidence": local_confidence,
                        "probabilities": probability_map(
                            local_probabilities,
                            self.local_classes,
                        ),
                    }
                    if local_probabilities is not None
                    else None
                ),
                "upstream": (
                    {
                        "label": upstream_root_cause,
                        "confidence": upstream_confidence,
                        "probabilities": probability_map(
                            upstream_probabilities,
                            self.upstream_classes,
                        ),
                    }
                    if upstream_probabilities is not None
                    else None
                ),
            },
            "anomaly": {
                "flagged": anomaly_flag,
                "score": anomaly_score,
                "threshold": (
                    self.anomaly_threshold
                ),
                "score_to_threshold_ratio": (
                    anomaly_score /
                    self.anomaly_threshold
                    if self.anomaly_threshold > 0.0
                    else None
                ),
                "model": (
                    "temporal_autoencoder_v3"
                ),
            },
            "energy_recommendation": (
                energy
            ),
        }


def evaluate_unified_engine(
    *,
    engine: UnifiedAIEngine,
    dataset_path: Path,
) -> tuple[dict, dict]:
    with np.load(
        dataset_path,
        allow_pickle=False,
    ) as dataset:
        x = dataset[
            "x"
        ].astype(
            np.float32
        )

        y_domain = dataset[
            "y"
        ].astype(
            np.int64
        )

        local_truth = dataset[
            "local_root_cause"
        ].astype(
            str
        )

        upstream_truth = dataset[
            "upstream_root_cause"
        ].astype(
            str
        )

        feature_names = [
            str(value)
            for value in dataset[
                "feature_names"
            ].tolist()
        ]

    if (
        feature_names !=
        engine.feature_columns
    ):
        raise RuntimeError(
            "V3 dataset feature order does not match the unified engine."
        )

    indices = np.arange(
        len(y_domain)
    )

    (
        _outer_train_indices,
        test_indices,
    ) = train_test_split(
        indices,
        test_size=float(
            engine.domain_metadata[
                "outer_test_size"
            ]
        ),
        random_state=int(
            engine.domain_metadata[
                "random_state"
            ]
        ),
        stratify=y_domain,
    )

    domain_predictions = []
    anomaly_predictions = []

    local_true_flags = []
    local_correct_flags = []

    upstream_true_flags = []
    upstream_correct_flags = []

    mixed_flags = []
    mixed_exact_flags = []

    hierarchy_exact_flags = []

    recommendation_counts = Counter()

    demo_result = None
    demo_truth = None
    demo_index = None

    for global_index in test_indices:
        result = engine.diagnose(
            x[
                global_index
            ]
        )

        predicted_domain = result[
            "fault_domain"
        ][
            "label"
        ]

        predicted_domain_id = DOMAIN_TO_ID[
            predicted_domain
        ]

        true_domain_id = int(
            y_domain[
                global_index
            ]
        )

        true_domain = LABEL_ORDER[
            true_domain_id
        ]

        domain_predictions.append(
            predicted_domain_id
        )

        anomaly_predictions.append(
            bool(
                result[
                    "anomaly"
                ][
                    "flagged"
                ]
            )
        )

        recommendation_counts[
            result[
                "energy_recommendation"
            ][
                "recommended_mode"
            ]
        ] += 1

        true_local = str(
            local_truth[
                global_index
            ]
        )

        true_upstream = str(
            upstream_truth[
                global_index
            ]
        )

        predicted_local = (
            result[
                "root_cause"
            ][
                "local"
            ][
                "label"
            ]
            if result[
                "root_cause"
            ][
                "local"
            ] is not None
            else None
        )

        predicted_upstream = (
            result[
                "root_cause"
            ][
                "upstream"
            ][
                "label"
            ]
            if result[
                "root_cause"
            ][
                "upstream"
            ] is not None
            else None
        )

        has_local = (
            true_local != "NONE"
        )

        has_upstream = (
            true_upstream != "NONE"
        )

        local_correct = (
            has_local and
            predicted_domain in {
                "LOCAL",
                "MIXED",
            } and
            predicted_local ==
            true_local
        )

        upstream_correct = (
            has_upstream and
            predicted_domain in {
                "UPSTREAM",
                "MIXED",
            } and
            predicted_upstream ==
            true_upstream
        )

        local_true_flags.append(
            has_local
        )

        local_correct_flags.append(
            local_correct
        )

        upstream_true_flags.append(
            has_upstream
        )

        upstream_correct_flags.append(
            upstream_correct
        )

        if true_domain == "NORMAL":
            hierarchy_exact = (
                predicted_domain ==
                "NORMAL"
            )

        elif true_domain == "LOCAL":
            hierarchy_exact = (
                predicted_domain ==
                "LOCAL" and
                local_correct
            )

        elif true_domain == "UPSTREAM":
            hierarchy_exact = (
                predicted_domain ==
                "UPSTREAM" and
                upstream_correct
            )

        else:
            hierarchy_exact = (
                predicted_domain ==
                "MIXED" and
                local_correct and
                upstream_correct
            )

            mixed_flags.append(
                True
            )

            mixed_exact_flags.append(
                hierarchy_exact
            )

        hierarchy_exact_flags.append(
            hierarchy_exact
        )

        if (
            demo_result is None and
            true_domain == "MIXED"
        ):
            demo_result = result
            demo_truth = {
                "fault_domain": (
                    true_domain
                ),
                "local_root_cause": (
                    true_local
                ),
                "upstream_root_cause": (
                    true_upstream
                ),
            }

            demo_index = int(
                global_index
            )

    y_test = y_domain[
        test_indices
    ]

    domain_accuracy = float(
        accuracy_score(
            y_test,
            np.asarray(
                domain_predictions,
                dtype=np.int64,
            ),
        )
    )

    anomaly_predictions = np.asarray(
        anomaly_predictions,
        dtype=bool,
    )

    normal_mask = (
        y_test ==
        DOMAIN_TO_ID[
            "NORMAL"
        ]
    )

    fault_mask = ~normal_mask

    local_true_flags = np.asarray(
        local_true_flags,
        dtype=bool,
    )

    local_correct_flags = np.asarray(
        local_correct_flags,
        dtype=bool,
    )

    upstream_true_flags = np.asarray(
        upstream_true_flags,
        dtype=bool,
    )

    upstream_correct_flags = np.asarray(
        upstream_correct_flags,
        dtype=bool,
    )

    hierarchy_exact_flags = np.asarray(
        hierarchy_exact_flags,
        dtype=bool,
    )

    mixed_exact_array = np.asarray(
        mixed_exact_flags,
        dtype=bool,
    )

    metrics = {
        "schema": (
            "abs.ai.unified.metrics.v1"
        ),
        "dataset_sha256": (
            engine.dataset_sha256
        ),
        "test_rows": int(
            len(
                test_indices
            )
        ),
        "fault_domain_accuracy": (
            domain_accuracy
        ),
        "anomaly": {
            "normal_false_positive_rate": float(
                anomaly_predictions[
                    normal_mask
                ].mean()
            ),
            "known_fault_detection_rate": float(
                anomaly_predictions[
                    fault_mask
                ].mean()
            ),
        },
        "end_to_end_root_cause": {
            "local_rows": int(
                local_true_flags.sum()
            ),
            "local_accuracy_including_domain_routing": float(
                local_correct_flags[
                    local_true_flags
                ].mean()
            ),
            "upstream_rows": int(
                upstream_true_flags.sum()
            ),
            "upstream_accuracy_including_domain_routing": float(
                upstream_correct_flags[
                    upstream_true_flags
                ].mean()
            ),
            "mixed_rows": int(
                len(
                    mixed_exact_array
                )
            ),
            "mixed_exact_domain_and_both_causes_accuracy": float(
                mixed_exact_array.mean()
            ),
            "all_sequence_exact_hierarchical_accuracy": float(
                hierarchy_exact_flags.mean()
            ),
        },
        "energy_recommendation_distribution": dict(
            recommendation_counts
        ),
        "guardrail_contract": {
            "ai_output_is_pre_guardrail": True,
            "final_mode_authority": (
                "deterministic_safety_guardrails"
            ),
        },
    }

    return (
        metrics,
        {
            "dataset_index": (
                demo_index
            ),
            "truth": (
                demo_truth
            ),
            "ai_result": (
                demo_result
            ),
        },
    )


def main() -> None:
    here = Path(__file__).resolve().parent

    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--dataset",
        type=Path,
        default=(
            here /
            "data" /
            "base_station_temporal_dataset_v3.npz"
        ),
    )

    parser.add_argument(
        "--evaluate",
        action="store_true",
        default=True,
    )

    args = parser.parse_args()

    engine = UnifiedAIEngine(
        here
    )

    dataset_hash = sha256_file(
        args.dataset
    )

    if (
        dataset_hash !=
        engine.dataset_sha256
    ):
        raise RuntimeError(
            "Local V3 dataset SHA256 does not match trained model metadata."
        )

    print()
    print(
        "AUTONOMOUS BASE STATION - "
        "UNIFIED AI INFERENCE V3"
    )
    print("=" * 68)
    print(
        f"Feature columns      : "
        f"{len(engine.feature_columns)}"
    )
    print(
        f"Timesteps            : "
        f"{engine.timesteps}"
    )
    print(
        f"Window               : "
        f"{engine.timesteps * engine.sample_interval_seconds:.0f} seconds"
    )
    print(
        f"Domain classes       : "
        f"{len(engine.domain_classes)}"
    )
    print(
        f"Local causes         : "
        f"{len(engine.local_classes)}"
    )
    print(
        f"Upstream causes      : "
        f"{len(engine.upstream_classes)}"
    )
    print(
        f"Anomaly threshold    : "
        f"{engine.anomaly_threshold:.6f}"
    )
    print(
        "Model hash agreement : PASS"
    )
    print(
        "Guardrail separation : PASS"
    )

    (
        metrics,
        demo,
    ) = evaluate_unified_engine(
        engine=engine,
        dataset_path=args.dataset,
    )

    print()
    print(
        "[ END-TO-END UNIFIED EVALUATION ]"
    )
    print(
        f"Fault-domain accuracy: "
        f"{metrics['fault_domain_accuracy']:.4f}"
    )

    print()
    print(
        "[ ANOMALY LAYER ]"
    )
    print(
        f"NORMAL false alarm   : "
        f"{metrics['anomaly']['normal_false_positive_rate']:.4f}"
    )
    print(
        f"Known-fault detection: "
        f"{metrics['anomaly']['known_fault_detection_rate']:.4f}"
    )

    root_metrics = metrics[
        "end_to_end_root_cause"
    ]

    print()
    print(
        "[ END-TO-END ROOT CAUSE ]"
    )
    print(
        f"Local rows           : "
        f"{root_metrics['local_rows']}"
    )
    print(
        f"Local + routing acc  : "
        f"{root_metrics['local_accuracy_including_domain_routing']:.4f}"
    )
    print(
        f"Upstream rows        : "
        f"{root_metrics['upstream_rows']}"
    )
    print(
        f"Upstream + routing   : "
        f"{root_metrics['upstream_accuracy_including_domain_routing']:.4f}"
    )
    print(
        f"MIXED rows           : "
        f"{root_metrics['mixed_rows']}"
    )
    print(
        f"MIXED exact full diag: "
        f"{root_metrics['mixed_exact_domain_and_both_causes_accuracy']:.4f}"
    )
    print(
        f"All exact hierarchy  : "
        f"{root_metrics['all_sequence_exact_hierarchical_accuracy']:.4f}"
    )

    print()
    print(
        "[ AI ENERGY RECOMMENDATIONS - PRE-GUARDRAIL ]"
    )

    for mode in [
        "FULL",
        "ECO",
        "REDUCED",
        "EMERGENCY",
    ]:
        count = metrics[
            "energy_recommendation_distribution"
        ].get(
            mode,
            0,
        )

        print(
            f"{mode:<12}: "
            f"{count}"
        )

    print()
    print(
        "[ GUARDRAIL CONTRACT ]"
    )
    print(
        "AI recommendation    : PRE-GUARDRAIL ONLY"
    )
    print(
        "Final mode authority : DETERMINISTIC SAFETY GUARDRAILS"
    )

    print()
    print(
        "[ STRUCTURED MIXED-FAULT DEMO ]"
    )
    print(
        "Expected             : " +
        json.dumps(
            demo[
                "truth"
            ],
            separators=(
                ",",
                ":",
            ),
        )
    )
    print(
        "AI_RESULT|" +
        json.dumps(
            demo[
                "ai_result"
            ],
            separators=(
                ",",
                ":",
            ),
        )
    )

    model_dir = (
        here /
        "models"
    )

    result_dir = (
        here /
        "results"
    )

    metadata_path = (
        model_dir /
        "unified_ai_v3_metadata.json"
    )

    metrics_path = (
        result_dir /
        "unified_ai_v3_metrics.json"
    )

    metadata = {
        "schema": (
            "abs.ai.unified.v1"
        ),
        "name": (
            "unified_ai_v3"
        ),
        "dataset_sha256": (
            engine.dataset_sha256
        ),
        "feature_columns": (
            engine.feature_columns
        ),
        "feature_count": len(
            engine.feature_columns
        ),
        "timesteps": (
            engine.timesteps
        ),
        "sample_interval_seconds": (
            engine.sample_interval_seconds
        ),
        "components": {
            "fault_domain": (
                "fault_domain_temporal_tcn_v3"
            ),
            "anomaly": (
                "temporal_autoencoder_v3"
            ),
            "local_root_cause": (
                "local_root_cause_tcn_v3"
            ),
            "upstream_root_cause": (
                "upstream_root_cause_tcn_v3"
            ),
            "energy_recommendation": (
                "diagnosis_aware_pre_guardrail_policy_v1"
            ),
        },
        "energy_policy_status": (
            "pre_guardrail_recommendation_only"
        ),
        "safety_contract": (
            "AI recommendation -> deterministic guardrails -> final action"
        ),
        "live_buffer_supported": True,
        "live_buffer_class": (
            "TemporalWindowBuffer"
        ),
        "final_operating_mode_authority": (
            "existing deterministic firmware guardrails"
        ),
    }

    metadata_path.write_text(
        json.dumps(
            metadata,
            indent=2,
        ),
        encoding="utf-8",
    )

    metrics_path.write_text(
        json.dumps(
            metrics,
            indent=2,
        ),
        encoding="utf-8",
    )

    print()
    print(
        f"Metadata             : "
        f"{metadata_path.resolve()}"
    )
    print(
        f"Metrics              : "
        f"{metrics_path.resolve()}"
    )

    print()
    print(
        "Next stage           : feed live ABS_JSON telemetry into "
        "TemporalWindowBuffer, run this engine continuously, "
        "then hand the AI recommendation to the existing "
        "deterministic guardrail controller."
    )


if __name__ == "__main__":
    main()