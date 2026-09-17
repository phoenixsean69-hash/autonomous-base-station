#!/usr/bin/env python3
"""
XGBoost fault-domain classifier for the Autonomous Base Station hybrid AI stage.

This intentionally reuses ai_common.py so XGBoost sees the SAME:
- 33 telemetry features
- leakage guard
- stratified 80/20 split
- random seed

as the Random Forest baseline.

Target:
    NORMAL / LOCAL / UPSTREAM / MIXED
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
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


LABEL_TO_ID = {
    label: index
    for index, label in enumerate(LABEL_ORDER)
}

ID_TO_LABEL = {
    index: label
    for label, index in LABEL_TO_ID.items()
}


def encode_labels(series):
    return np.asarray(
        [
            LABEL_TO_ID[value]
            for value in series
        ],
        dtype=np.int64,
    )


def decode_labels(values):
    return np.asarray(
        [
            ID_TO_LABEL[int(value)]
            for value in values
        ],
        dtype=object,
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

    parser.add_argument(
        "--trees",
        type=int,
        default=500,
    )

    args = parser.parse_args()

    df = load_dataset(args.dataset)
    feature_columns = build_feature_list(df)

    (
        x_train,
        x_test,
        y_train_text,
        y_test_text,
    ) = split_fault_domain_dataset(
        df,
        feature_columns,
        args.test_size,
        args.seed,
    )

    y_train = encode_labels(
        y_train_text
    )

    y_test = encode_labels(
        y_test_text
    )

    model = XGBClassifier(
        objective="multi:softprob",
        num_class=len(LABEL_ORDER),
        n_estimators=args.trees,
        max_depth=6,
        learning_rate=0.05,
        min_child_weight=2.0,
        subsample=0.90,
        colsample_bytree=0.85,
        reg_alpha=0.05,
        reg_lambda=1.20,
        gamma=0.0,
        tree_method="hist",
        eval_metric="mlogloss",
        random_state=args.seed,
        n_jobs=-1,
    )

    print()
    print(
        "AUTONOMOUS BASE STATION - "
        "HYBRID AI STAGE / XGBOOST"
    )
    print("=" * 64)
    print(f"Dataset rows      : {len(df)}")
    print(f"Feature columns   : {len(feature_columns)}")
    print(f"Training rows     : {len(x_train)}")
    print(f"Test rows         : {len(x_test)}")
    print(f"Trees             : {args.trees}")
    print("Leakage guard     : PASS")
    print("Comparison split  : SAME AS RANDOM FOREST")
    print()
    print("Training XGBoost classifier...")

    model.fit(
        x_train,
        y_train,
        verbose=False,
    )

    predicted_ids = model.predict(
        x_test
    )

    probabilities = model.predict_proba(
        x_test
    )

    predictions = decode_labels(
        predicted_ids
    )

    y_test_labels = np.asarray(
        y_test_text,
        dtype=object,
    )

    accuracy = accuracy_score(
        y_test_labels,
        predictions,
    )

    balanced_accuracy = balanced_accuracy_score(
        y_test_labels,
        predictions,
    )

    macro_precision = precision_score(
        y_test_labels,
        predictions,
        labels=LABEL_ORDER,
        average="macro",
        zero_division=0,
    )

    macro_recall = recall_score(
        y_test_labels,
        predictions,
        labels=LABEL_ORDER,
        average="macro",
        zero_division=0,
    )

    macro_f1 = f1_score(
        y_test_labels,
        predictions,
        labels=LABEL_ORDER,
        average="macro",
        zero_division=0,
    )

    multiclass_log_loss = log_loss(
        y_test,
        probabilities,
        labels=list(
            range(len(LABEL_ORDER))
        ),
    )

    matrix = confusion_matrix(
        y_test_labels,
        predictions,
        labels=LABEL_ORDER,
    )

    report = classification_report(
        y_test_labels,
        predictions,
        labels=LABEL_ORDER,
        output_dict=True,
        zero_division=0,
    )

    importance_pairs = sorted(
        zip(
            feature_columns,
            model.feature_importances_,
        ),
        key=lambda item: item[1],
        reverse=True,
    )

    model_dir = here / "models"
    result_dir = here / "results"

    model_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    result_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    model_path = (
        model_dir /
        "fault_domain_xgboost_v2.json"
    )

    metadata_path = (
        model_dir /
        "fault_domain_xgb_v2_metadata.json"
    )

    metrics_path = (
        result_dir /
        "fault_domain_xgb_v2_metrics.json"
    )

    model.save_model(
        model_path
    )

    dataset_hash = sha256_file(
        args.dataset
    )

    metadata = {
        "schema": "abs.ml.model.v1",
        "model_name": "fault_domain_xgboost_v2",
        "model_type": "XGBClassifier",
        "hybrid_ai_role": "boosted_tree_model",
        "target": "fault_domain",
        "classes": LABEL_ORDER,
        "label_to_id": LABEL_TO_ID,
        "feature_columns": feature_columns,
        "feature_count": len(feature_columns),
        "dataset_sha256": dataset_hash,
        "test_size": args.test_size,
        "random_state": args.seed,
        "n_estimators": args.trees,
        "max_depth": 6,
        "learning_rate": 0.05,
    }

    metrics = {
        "schema": "abs.ml.metrics.v1",
        "model_name": "fault_domain_xgboost_v2",
        "dataset_sha256": dataset_hash,
        "dataset_rows": len(df),
        "training_rows": len(x_train),
        "test_rows": len(x_test),
        "accuracy": accuracy,
        "balanced_accuracy": balanced_accuracy,
        "macro_precision": macro_precision,
        "macro_recall": macro_recall,
        "macro_f1": macro_f1,
        "log_loss": multiclass_log_loss,
        "label_order": LABEL_ORDER,
        "confusion_matrix": matrix.tolist(),
        "classification_report": report,
        "feature_importance": [
            {
                "feature": feature,
                "importance": float(
                    importance
                ),
            }
            for feature, importance
            in importance_pairs
        ],
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
    print("[ EVALUATION ]")
    print(f"Accuracy          : {accuracy:.4f}")
    print(
        f"Balanced Accuracy : "
        f"{balanced_accuracy:.4f}"
    )
    print(
        f"Macro Precision   : "
        f"{macro_precision:.4f}"
    )
    print(
        f"Macro Recall      : "
        f"{macro_recall:.4f}"
    )
    print(f"Macro F1          : {macro_f1:.4f}")
    print(
        f"Log Loss          : "
        f"{multiclass_log_loss:.4f}"
    )

    print()
    print("[ CONFUSION MATRIX ]")
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
                f"{value:>8}"
                for value in row
            )
        )

    print()
    print("[ CLASS RECALL ]")

    for label, row in zip(
        LABEL_ORDER,
        matrix,
    ):
        total = int(
            np.sum(row)
        )

        correct = int(
            row[
                LABEL_ORDER.index(label)
            ]
        )

        recall = (
            correct / total
            if total
            else 0.0
        )

        print(
            f"{label:<12}: "
            f"{recall:.4f}"
        )

    print()
    print("[ TOP 12 FEATURES ]")

    for index, (
        feature,
        importance,
    ) in enumerate(
        importance_pairs[:12],
        start=1,
    ):
        print(
            f"{index:>2}. "
            f"{feature:<32} "
            f"{importance:.4f}"
        )

    print()
    print(
        f"Model             : "
        f"{model_path.resolve()}"
    )
    print(
        f"Metadata          : "
        f"{metadata_path.resolve()}"
    )
    print(
        f"Metrics           : "
        f"{metrics_path.resolve()}"
    )
    print()
    print(
        "Next hybrid stage : "
        "compare RF vs XGBoost, then "
        "add deep learning."
    )


if __name__ == "__main__":
    main()