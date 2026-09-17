#!/usr/bin/env python3
"""
Random Forest baseline for Autonomous Base Station fault-domain diagnosis.

This is intentionally kept as a first-class model in the hybrid AI stage:
    Random Forest + XGBoost + Deep Learning + anomaly detection.

Later models will reuse ai_common.py so all comparisons use the same
feature set, leakage guard and deterministic holdout split.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import joblib
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import (
    accuracy_score,
    balanced_accuracy_score,
    classification_report,
    confusion_matrix,
    f1_score,
    precision_score,
    recall_score,
)

from ai_common import (
    LABEL_ORDER,
    build_feature_list,
    load_dataset,
    sha256_file,
    split_fault_domain_dataset,
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
            "base_station_fault_dataset.csv"
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
        default=400,
    )

    args = parser.parse_args()

    df = load_dataset(args.dataset)
    feature_columns = build_feature_list(df)

    (
        x_train,
        x_test,
        y_train,
        y_test,
    ) = split_fault_domain_dataset(
        df,
        feature_columns,
        args.test_size,
        args.seed,
    )

    model = RandomForestClassifier(
        n_estimators=args.trees,
        max_depth=14,
        min_samples_leaf=2,
        max_features="sqrt",
        class_weight="balanced_subsample",
        bootstrap=True,
        oob_score=True,
        random_state=args.seed,
        n_jobs=-1,
    )

    print()
    print(
        "AUTONOMOUS BASE STATION - "
        "HYBRID AI STAGE / RANDOM FOREST"
    )
    print("=" * 64)
    print(f"Dataset rows      : {len(df)}")
    print(f"Feature columns   : {len(feature_columns)}")
    print(f"Training rows     : {len(x_train)}")
    print(f"Test rows         : {len(x_test)}")
    print(f"Trees             : {args.trees}")
    print("Leakage guard     : PASS")
    print()
    print("Training Random Forest baseline...")

    model.fit(
        x_train,
        y_train,
    )

    predictions = model.predict(x_test)

    accuracy = accuracy_score(
        y_test,
        predictions,
    )

    balanced_accuracy = balanced_accuracy_score(
        y_test,
        predictions,
    )

    macro_precision = precision_score(
        y_test,
        predictions,
        labels=LABEL_ORDER,
        average="macro",
        zero_division=0,
    )

    macro_recall = recall_score(
        y_test,
        predictions,
        labels=LABEL_ORDER,
        average="macro",
        zero_division=0,
    )

    macro_f1 = f1_score(
        y_test,
        predictions,
        labels=LABEL_ORDER,
        average="macro",
        zero_division=0,
    )

    matrix = confusion_matrix(
        y_test,
        predictions,
        labels=LABEL_ORDER,
    )

    report = classification_report(
        y_test,
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
        "fault_domain_random_forest.joblib"
    )

    metadata_path = (
        model_dir /
        "fault_domain_rf_metadata.json"
    )

    metrics_path = (
        result_dir /
        "fault_domain_rf_metrics.json"
    )

    joblib.dump(
        model,
        model_path,
    )

    dataset_hash = sha256_file(
        args.dataset
    )

    metadata = {
        "schema": "abs.ml.model.v1",
        "model_name": "fault_domain_random_forest",
        "model_type": "RandomForestClassifier",
        "hybrid_ai_role": "baseline_and_embedded_candidate",
        "target": "fault_domain",
        "classes": list(model.classes_),
        "feature_columns": feature_columns,
        "feature_count": len(feature_columns),
        "dataset_sha256": dataset_hash,
        "test_size": args.test_size,
        "random_state": args.seed,
        "n_estimators": args.trees,
        "max_depth": 14,
        "min_samples_leaf": 2,
    }

    metrics = {
        "schema": "abs.ml.metrics.v1",
        "model_name": "fault_domain_random_forest",
        "dataset_sha256": dataset_hash,
        "dataset_rows": len(df),
        "training_rows": len(x_train),
        "test_rows": len(x_test),
        "accuracy": accuracy,
        "balanced_accuracy": balanced_accuracy,
        "macro_precision": macro_precision,
        "macro_recall": macro_recall,
        "macro_f1": macro_f1,
        "oob_score": model.oob_score_,
        "label_order": LABEL_ORDER,
        "confusion_matrix": matrix.tolist(),
        "classification_report": report,
        "feature_importance": [
            {
                "feature": feature,
                "importance": float(importance),
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
    print(f"OOB Score         : {model.oob_score_:.4f}")

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
        "XGBoost on the SAME split/features, "
        "then deep learning."
    )


if __name__ == "__main__":
    main()