#!/usr/bin/env python3
"""
Evaluate the Autonomous Base Station hybrid fault-domain ensemble.

No ensemble weights are tuned on the test set.

Two pre-declared rules:
1. Equal soft voting: average RF, XGBoost and Deep MLP probabilities.
2. Majority hard voting: two-of-three vote, with equal-soft tie break.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import joblib
import numpy as np
import torch
from sklearn.metrics import (
    accuracy_score,
    balanced_accuracy_score,
    classification_report,
    confusion_matrix,
    f1_score,
    log_loss,
    precision_score,
    recall_score,
)
from xgboost import XGBClassifier

from ai_common import (
    LABEL_ORDER,
    build_feature_list,
    load_dataset,
    sha256_file,
    split_fault_domain_dataset,
)
from train_deep_mlp import FaultDomainMLP


LABEL_TO_ID = {
    label: index
    for index, label in enumerate(LABEL_ORDER)
}

ID_TO_LABEL = {
    index: label
    for label, index in LABEL_TO_ID.items()
}


def read_json(path: Path) -> dict:
    if not path.exists():
        raise FileNotFoundError(
            f"Required metadata file not found: {path}"
        )

    return json.loads(
        path.read_text(encoding="utf-8")
    )


def reorder_probability_columns(
    probabilities: np.ndarray,
    source_classes,
) -> np.ndarray:
    source_classes = [
        str(value)
        for value in source_classes
    ]

    missing = (
        set(LABEL_ORDER) -
        set(source_classes)
    )

    if missing:
        raise RuntimeError(
            "Probability source is missing classes: " +
            ", ".join(sorted(missing))
        )

    indices = [
        source_classes.index(label)
        for label in LABEL_ORDER
    ]

    return probabilities[:, indices]


def normalize_probabilities(
    probabilities: np.ndarray,
) -> np.ndarray:
    """
    Normalize every probability row to sum exactly to one.

    Some model libraries return float32 probabilities whose row sums differ
    from 1.0 by tiny numerical amounts. sklearn.log_loss warns about this
    even though class predictions are unaffected.
    """
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


def decode_ids(ids) -> np.ndarray:
    return np.asarray(
        [
            ID_TO_LABEL[int(value)]
            for value in ids
        ],
        dtype=object,
    )


def calculate_metrics(
    y_true,
    predictions,
    probabilities,
) -> dict:
    return {
        "accuracy": accuracy_score(
            y_true,
            predictions,
        ),
        "balanced_accuracy": balanced_accuracy_score(
            y_true,
            predictions,
        ),
        "macro_precision": precision_score(
            y_true,
            predictions,
            labels=LABEL_ORDER,
            average="macro",
            zero_division=0,
        ),
        "macro_recall": recall_score(
            y_true,
            predictions,
            labels=LABEL_ORDER,
            average="macro",
            zero_division=0,
        ),
        "macro_f1": f1_score(
            y_true,
            predictions,
            labels=LABEL_ORDER,
            average="macro",
            zero_division=0,
        ),
        "log_loss": log_loss(
            [
                LABEL_TO_ID[label]
                for label in y_true
            ],
            probabilities,
            labels=list(range(len(LABEL_ORDER))),
        ),
        "confusion_matrix": confusion_matrix(
            y_true,
            predictions,
            labels=LABEL_ORDER,
        ).tolist(),
        "classification_report": classification_report(
            y_true,
            predictions,
            labels=LABEL_ORDER,
            output_dict=True,
            zero_division=0,
        ),
    }


def print_model_metrics(
    name: str,
    metrics: dict,
) -> None:
    print()
    print(f"[ {name} ]")
    print(f"Accuracy          : {metrics['accuracy']:.4f}")
    print(f"Balanced Accuracy : {metrics['balanced_accuracy']:.4f}")
    print(f"Macro Precision   : {metrics['macro_precision']:.4f}")
    print(f"Macro Recall      : {metrics['macro_recall']:.4f}")
    print(f"Macro F1          : {metrics['macro_f1']:.4f}")
    print(f"Log Loss          : {metrics['log_loss']:.4f}")


def print_confusion_matrix(
    title: str,
    matrix,
) -> None:
    print()
    print(f"[ {title} - CONFUSION MATRIX ]")
    print(
        "Actual \\ Pred   " +
        " ".join(
            f"{label[:8]:>8}"
            for label in LABEL_ORDER
        )
    )

    for label, row in zip(
        LABEL_ORDER,
        matrix,
    ):
        print(
            f"{label[:13]:<13} " +
            " ".join(
                f"{int(value):>8}"
                for value in row
            )
        )


def print_class_recall(
    title: str,
    matrix,
) -> None:
    print()
    print(f"[ {title} - CLASS RECALL ]")

    for index, label in enumerate(LABEL_ORDER):
        row = np.asarray(
            matrix[index],
            dtype=float,
        )
        total = float(row.sum())
        recall = (
            row[index] / total
            if total
            else 0.0
        )
        print(
            f"{label:<12}: {recall:.4f}"
        )


def load_mlp_probabilities(
    model_path: Path,
    scaler_path: Path,
    x_test,
    input_dim: int,
) -> np.ndarray:
    scaler = joblib.load(
        scaler_path
    )

    x_scaled = scaler.transform(
        x_test
    ).astype(np.float32)

    checkpoint = torch.load(
        model_path,
        map_location="cpu",
        weights_only=False,
    )

    model = FaultDomainMLP(
        input_dim=input_dim,
        output_dim=len(LABEL_ORDER),
    )

    model.load_state_dict(
        checkpoint["state_dict"]
    )

    model.eval()

    with torch.no_grad():
        logits = model(
            torch.tensor(
                x_scaled,
                dtype=torch.float32,
            )
        )

        probabilities = torch.softmax(
            logits,
            dim=1,
        ).cpu().numpy()

    return probabilities


def majority_vote(
    prediction_matrix: np.ndarray,
    soft_probabilities: np.ndarray,
) -> np.ndarray:
    output = []

    for row_index, row in enumerate(prediction_matrix):
        counts = np.bincount(
            row,
            minlength=len(LABEL_ORDER),
        )

        winners = np.flatnonzero(
            counts == counts.max()
        )

        if len(winners) == 1:
            output.append(
                int(winners[0])
            )
        else:
            output.append(
                int(
                    np.argmax(
                        soft_probabilities[row_index]
                    )
                )
            )

    return np.asarray(
        output,
        dtype=np.int64,
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
            "base_station_fault_dataset_v2.csv"
        ),
    )

    parser.add_argument(
        "--test-size",
        type=float,
        default=0.20,
    )

    parser.add_argument(
        "--seed",
        type=int,
        default=42,
    )

    args = parser.parse_args()

    model_dir = here / "models"
    result_dir = here / "results"

    rf_path = (
        model_dir /
        "fault_domain_random_forest_v2.joblib"
    )
    xgb_path = (
        model_dir /
        "fault_domain_xgboost_v2.json"
    )
    mlp_path = (
        model_dir /
        "fault_domain_deep_mlp_v2.pt"
    )
    mlp_scaler_path = (
        model_dir /
        "fault_domain_deep_mlp_v2_scaler.joblib"
    )

    rf_meta_path = (
        model_dir /
        "fault_domain_rf_v2_metadata.json"
    )
    xgb_meta_path = (
        model_dir /
        "fault_domain_xgb_v2_metadata.json"
    )
    mlp_meta_path = (
        model_dir /
        "fault_domain_deep_mlp_v2_metadata.json"
    )

    for path in [
        rf_path,
        xgb_path,
        mlp_path,
        mlp_scaler_path,
    ]:
        if not path.exists():
            raise FileNotFoundError(
                "\nRequired local model artifact is missing:\n"
                f"  {path}\n\n"
                "Model binaries are intentionally ignored by Git."
            )

    df = load_dataset(
        args.dataset
    )

    feature_columns = build_feature_list(
        df
    )

    (
        _x_train,
        x_test,
        _y_train,
        y_test,
    ) = split_fault_domain_dataset(
        df,
        feature_columns,
        args.test_size,
        args.seed,
    )

    dataset_hash = sha256_file(
        args.dataset
    )

    rf_meta = read_json(
        rf_meta_path
    )
    xgb_meta = read_json(
        xgb_meta_path
    )
    mlp_meta = read_json(
        mlp_meta_path
    )

    for name, metadata in [
        ("Random Forest", rf_meta),
        ("XGBoost", xgb_meta),
        ("Deep MLP", mlp_meta),
    ]:
        if metadata.get("dataset_sha256") != dataset_hash:
            raise RuntimeError(
                f"{name} metadata does not match the V2 dataset hash."
            )

        if metadata.get("feature_columns") != feature_columns:
            raise RuntimeError(
                f"{name} feature order does not match the current V2 dataset."
            )

        if int(metadata.get("random_state", -1)) != args.seed:
            raise RuntimeError(
                f"{name} seed does not match ensemble seed {args.seed}."
            )

    rf_model = joblib.load(
        rf_path
    )
    rf_probabilities = normalize_probabilities(
        reorder_probability_columns(
            rf_model.predict_proba(x_test),
            rf_model.classes_,
        )
    )

    xgb_model = XGBClassifier()
    xgb_model.load_model(
        xgb_path
    )
    xgb_probabilities = normalize_probabilities(
        reorder_probability_columns(
            xgb_model.predict_proba(x_test),
            xgb_meta["classes"],
        )
    )

    mlp_probabilities = normalize_probabilities(
        reorder_probability_columns(
            load_mlp_probabilities(
                mlp_path,
                mlp_scaler_path,
                x_test,
                len(feature_columns),
            ),
            mlp_meta["classes"],
        )
    )

    rf_ids = np.argmax(
        rf_probabilities,
        axis=1,
    )
    xgb_ids = np.argmax(
        xgb_probabilities,
        axis=1,
    )
    mlp_ids = np.argmax(
        mlp_probabilities,
        axis=1,
    )

    rf_predictions = decode_ids(
        rf_ids
    )
    xgb_predictions = decode_ids(
        xgb_ids
    )
    mlp_predictions = decode_ids(
        mlp_ids
    )

    equal_soft_probabilities = (
        rf_probabilities +
        xgb_probabilities +
        mlp_probabilities
    ) / 3.0

    equal_soft_ids = np.argmax(
        equal_soft_probabilities,
        axis=1,
    )
    equal_soft_predictions = decode_ids(
        equal_soft_ids
    )

    prediction_matrix = np.column_stack(
        [
            rf_ids,
            xgb_ids,
            mlp_ids,
        ]
    )

    majority_ids = majority_vote(
        prediction_matrix,
        equal_soft_probabilities,
    )
    majority_predictions = decode_ids(
        majority_ids
    )

    rf_metrics = calculate_metrics(
        y_test,
        rf_predictions,
        rf_probabilities,
    )
    xgb_metrics = calculate_metrics(
        y_test,
        xgb_predictions,
        xgb_probabilities,
    )
    mlp_metrics = calculate_metrics(
        y_test,
        mlp_predictions,
        mlp_probabilities,
    )
    equal_soft_metrics = calculate_metrics(
        y_test,
        equal_soft_predictions,
        equal_soft_probabilities,
    )
    majority_metrics = calculate_metrics(
        y_test,
        majority_predictions,
        equal_soft_probabilities,
    )

    unanimous_mask = (
        (rf_ids == xgb_ids) &
        (xgb_ids == mlp_ids)
    )
    disagreement_mask = ~unanimous_mask

    unanimous_count = int(
        unanimous_mask.sum()
    )
    disagreement_count = int(
        disagreement_mask.sum()
    )

    y_test_array = np.asarray(
        y_test,
        dtype=object,
    )

    unanimous_accuracy = (
        accuracy_score(
            y_test_array[unanimous_mask],
            equal_soft_predictions[unanimous_mask],
        )
        if unanimous_count
        else 0.0
    )

    disagreement_accuracy = (
        accuracy_score(
            y_test_array[disagreement_mask],
            equal_soft_predictions[disagreement_mask],
        )
        if disagreement_count
        else 0.0
    )

    any_model_correct = (
        (rf_predictions == y_test_array) |
        (xgb_predictions == y_test_array) |
        (mlp_predictions == y_test_array)
    )

    oracle_upper_bound = float(
        any_model_correct.mean()
    )

    result_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    metadata_path = (
        model_dir /
        "fault_domain_ensemble_v2_metadata.json"
    )
    metrics_path = (
        result_dir /
        "fault_domain_ensemble_v2_metrics.json"
    )

    metadata = {
        "schema": "abs.ml.ensemble.v1",
        "ensemble_name": "fault_domain_ensemble_v2",
        "target": "fault_domain",
        "dataset_sha256": dataset_hash,
        "feature_columns": feature_columns,
        "feature_count": len(feature_columns),
        "test_size": args.test_size,
        "random_state": args.seed,
        "components": [
            "fault_domain_random_forest_v2",
            "fault_domain_xgboost_v2",
            "fault_domain_deep_mlp_v2",
        ],
        "rules": {
            "equal_soft_vote": {
                "weights": {
                    "random_forest": 1.0 / 3.0,
                    "xgboost": 1.0 / 3.0,
                    "deep_mlp": 1.0 / 3.0,
                },
                "weight_tuning": "none",
            },
            "majority_hard_vote": {
                "rule": "two_of_three",
                "tie_break": "equal_soft_vote",
            },
        },
        "fairness_note": (
            "No ensemble weights were tuned on the test set."
        ),
    }

    metrics = {
        "schema": "abs.ml.metrics.v1",
        "model_name": "fault_domain_ensemble_v2",
        "dataset_sha256": dataset_hash,
        "dataset_rows": len(df),
        "test_rows": len(x_test),
        "component_metrics": {
            "random_forest": rf_metrics,
            "xgboost": xgb_metrics,
            "deep_mlp": mlp_metrics,
        },
        "ensemble_metrics": {
            "equal_soft_vote": equal_soft_metrics,
            "majority_hard_vote": majority_metrics,
        },
        "agreement_analysis": {
            "unanimous_count": unanimous_count,
            "unanimous_pct": unanimous_count / len(x_test),
            "unanimous_accuracy": unanimous_accuracy,
            "disagreement_count": disagreement_count,
            "disagreement_pct": disagreement_count / len(x_test),
            "equal_soft_accuracy_on_disagreement": disagreement_accuracy,
            "oracle_upper_bound_if_any_component_correct": oracle_upper_bound,
        },
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
        "AUTONOMOUS BASE STATION - "
        "HYBRID AI ENSEMBLE V2"
    )
    print("=" * 64)
    print(f"Dataset rows      : {len(df)}")
    print(f"Test rows         : {len(x_test)}")
    print(f"Feature columns   : {len(feature_columns)}")
    print("Leakage guard     : PASS")
    print("Test split        : SAME AS RF/XGBOOST/MLP")
    print("Weight tuning     : NONE")

    print_model_metrics(
        "RANDOM FOREST",
        rf_metrics,
    )
    print_model_metrics(
        "XGBOOST",
        xgb_metrics,
    )
    print_model_metrics(
        "DEEP MLP",
        mlp_metrics,
    )
    print_model_metrics(
        "ENSEMBLE - EQUAL SOFT VOTE",
        equal_soft_metrics,
    )
    print_model_metrics(
        "ENSEMBLE - MAJORITY HARD VOTE",
        majority_metrics,
    )

    print_confusion_matrix(
        "EQUAL SOFT VOTE",
        equal_soft_metrics["confusion_matrix"],
    )
    print_class_recall(
        "EQUAL SOFT VOTE",
        equal_soft_metrics["confusion_matrix"],
    )

    print()
    print("[ MODEL AGREEMENT ]")
    print(
        f"Unanimous         : "
        f"{unanimous_count} / {len(x_test)} "
        f"({100.0 * unanimous_count / len(x_test):.1f}%)"
    )
    print(
        f"Unanimous accuracy: {unanimous_accuracy:.4f}"
    )
    print(
        f"Disagreement      : "
        f"{disagreement_count} / {len(x_test)} "
        f"({100.0 * disagreement_count / len(x_test):.1f}%)"
    )
    print(
        f"Soft-vote accuracy on disagreements: "
        f"{disagreement_accuracy:.4f}"
    )
    print(
        f"Any-model-correct upper bound       : "
        f"{oracle_upper_bound:.4f}"
    )

    print()
    print(f"Metadata          : {metadata_path.resolve()}")
    print(f"Metrics           : {metrics_path.resolve()}")
    print()
    print(
        "Next decision     : compare the ensemble "
        "against XGBoost without tuning on the test set."
    )


if __name__ == "__main__":
    main()