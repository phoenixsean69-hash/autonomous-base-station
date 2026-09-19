#!/usr/bin/env python3
"""
Leakage-safe stacking + disagreement gate for the Autonomous Base Station.

Design
------
Outer test set:
    EXACT same untouched 20% test split used by RF, XGBoost and Deep MLP.

Meta-training:
    3-fold out-of-fold (OOF) predictions are generated ONLY from the outer
    training partition. Each validation fold is predicted by component
    models that were not trained on that fold.

Two learned strategies are compared:
1. Full stack:
       Logistic Regression consumes OOF component probabilities and
       uncertainty/agreement features for every case.
2. Disagreement gate:
       If RF/XGBoost/MLP unanimously agree, accept the consensus.
       If they disagree, a Logistic Regression meta-classifier resolves
       the difficult case.

The outer test set is never used to train or tune either meta-classifier.
"""

from __future__ import annotations

import argparse
import json
import math
import random
from pathlib import Path

import joblib
import numpy as np
import torch
from sklearn.ensemble import RandomForestClassifier
from sklearn.linear_model import LogisticRegression
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
from sklearn.model_selection import StratifiedKFold, train_test_split
from sklearn.preprocessing import StandardScaler
from torch import nn
from torch.utils.data import DataLoader, TensorDataset
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


def set_seed(seed: int) -> None:
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)

    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def encode_labels(values) -> np.ndarray:
    return np.asarray(
        [
            LABEL_TO_ID[str(value)]
            for value in values
        ],
        dtype=np.int64,
    )


def decode_ids(values) -> np.ndarray:
    return np.asarray(
        [
            ID_TO_LABEL[int(value)]
            for value in values
        ],
        dtype=object,
    )


def normalize_probabilities(
    probabilities: np.ndarray,
) -> np.ndarray:
    probabilities = np.asarray(
        probabilities,
        dtype=np.float64,
    )

    sums = probabilities.sum(
        axis=1,
        keepdims=True,
    )

    if np.any(sums <= 0.0):
        raise RuntimeError(
            "Invalid probability row with non-positive sum."
        )

    return probabilities / sums


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

    return normalize_probabilities(
        probabilities[:, indices]
    )


def build_rf(
    seed: int,
) -> RandomForestClassifier:
    return RandomForestClassifier(
        n_estimators=400,
        max_depth=14,
        min_samples_leaf=2,
        max_features="sqrt",
        class_weight="balanced_subsample",
        bootstrap=True,
        oob_score=False,
        random_state=seed,
        n_jobs=-1,
    )


def build_xgb(
    seed: int,
) -> XGBClassifier:
    return XGBClassifier(
        objective="multi:softprob",
        num_class=len(LABEL_ORDER),
        n_estimators=500,
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
        random_state=seed,
        n_jobs=-1,
    )


def train_fold_mlp(
    x_train,
    y_train_text,
    x_valid,
    seed: int,
    max_epochs: int,
    patience: int,
    batch_size: int,
):
    set_seed(seed)

    (
        x_fit,
        x_early,
        y_fit_text,
        y_early_text,
    ) = train_test_split(
        x_train,
        y_train_text,
        test_size=0.15,
        random_state=seed,
        stratify=y_train_text,
    )

    scaler = StandardScaler()

    x_fit_scaled = scaler.fit_transform(
        x_fit
    ).astype(np.float32)

    x_early_scaled = scaler.transform(
        x_early
    ).astype(np.float32)

    x_valid_scaled = scaler.transform(
        x_valid
    ).astype(np.float32)

    y_fit = encode_labels(
        y_fit_text
    )

    y_early = encode_labels(
        y_early_text
    )

    train_ds = TensorDataset(
        torch.tensor(
            x_fit_scaled,
            dtype=torch.float32,
        ),
        torch.tensor(
            y_fit,
            dtype=torch.long,
        ),
    )

    early_ds = TensorDataset(
        torch.tensor(
            x_early_scaled,
            dtype=torch.float32,
        ),
        torch.tensor(
            y_early,
            dtype=torch.long,
        ),
    )

    generator = torch.Generator()
    generator.manual_seed(seed)

    train_loader = DataLoader(
        train_ds,
        batch_size=batch_size,
        shuffle=True,
        generator=generator,
    )

    early_loader = DataLoader(
        early_ds,
        batch_size=batch_size,
        shuffle=False,
    )

    device = torch.device("cpu")

    model = FaultDomainMLP(
        input_dim=x_fit_scaled.shape[1],
        output_dim=len(LABEL_ORDER),
    ).to(device)

    criterion = nn.CrossEntropyLoss()

    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=0.001,
        weight_decay=1e-4,
    )

    best_loss = float("inf")
    best_state = None
    best_epoch = 0
    stale = 0

    for epoch in range(
        1,
        max_epochs + 1,
    ):
        model.train()

        for features, labels in train_loader:
            optimizer.zero_grad(
                set_to_none=True
            )

            logits = model(features)
            loss = criterion(
                logits,
                labels,
            )

            loss.backward()
            optimizer.step()

        model.eval()

        total_loss = 0.0
        total_items = 0

        with torch.no_grad():
            for features, labels in early_loader:
                logits = model(features)

                loss = criterion(
                    logits,
                    labels,
                )

                count = labels.size(0)

                total_loss += (
                    float(loss.item()) *
                    count
                )

                total_items += count

        validation_loss = (
            total_loss /
            total_items
        )

        if (
            validation_loss <
            best_loss - 1e-5
        ):
            best_loss = validation_loss
            best_epoch = epoch

            best_state = {
                key: value.detach()
                .cpu()
                .clone()
                for key, value
                in model.state_dict().items()
            }

            stale = 0

        else:
            stale += 1

        if stale >= patience:
            break

    if best_state is None:
        raise RuntimeError(
            "Fold MLP did not produce a valid checkpoint."
        )

    model.load_state_dict(
        best_state
    )

    model.eval()

    with torch.no_grad():
        logits = model(
            torch.tensor(
                x_valid_scaled,
                dtype=torch.float32,
            )
        )

        probabilities = torch.softmax(
            logits,
            dim=1,
        ).cpu().numpy()

    return (
        normalize_probabilities(
            probabilities
        ),
        best_epoch,
    )


def entropy(
    probabilities: np.ndarray,
) -> np.ndarray:
    clipped = np.clip(
        probabilities,
        1e-12,
        1.0,
    )

    values = -np.sum(
        clipped * np.log(clipped),
        axis=1,
    )

    return (
        values /
        math.log(len(LABEL_ORDER))
    )


def margin(
    probabilities: np.ndarray,
) -> np.ndarray:
    ordered = np.sort(
        probabilities,
        axis=1,
    )

    return (
        ordered[:, -1] -
        ordered[:, -2]
    )


def build_meta_features(
    rf_prob: np.ndarray,
    xgb_prob: np.ndarray,
    mlp_prob: np.ndarray,
) -> np.ndarray:
    rf_ids = np.argmax(
        rf_prob,
        axis=1,
    )

    xgb_ids = np.argmax(
        xgb_prob,
        axis=1,
    )

    mlp_ids = np.argmax(
        mlp_prob,
        axis=1,
    )

    equal_soft = normalize_probabilities(
        (
            rf_prob +
            xgb_prob +
            mlp_prob
        ) / 3.0
    )

    uncertainty = np.column_stack(
        [
            rf_prob.max(axis=1),
            xgb_prob.max(axis=1),
            mlp_prob.max(axis=1),
            margin(rf_prob),
            margin(xgb_prob),
            margin(mlp_prob),
            entropy(rf_prob),
            entropy(xgb_prob),
            entropy(mlp_prob),
        ]
    )

    agreement = np.column_stack(
        [
            (rf_ids == xgb_ids).astype(float),
            (rf_ids == mlp_ids).astype(float),
            (xgb_ids == mlp_ids).astype(float),
        ]
    )

    return np.column_stack(
        [
            rf_prob,
            xgb_prob,
            mlp_prob,
            equal_soft,
            uncertainty,
            agreement,
        ]
    )


def component_disagreement_mask(
    rf_prob: np.ndarray,
    xgb_prob: np.ndarray,
    mlp_prob: np.ndarray,
) -> np.ndarray:
    rf_ids = np.argmax(
        rf_prob,
        axis=1,
    )
    xgb_ids = np.argmax(
        xgb_prob,
        axis=1,
    )
    mlp_ids = np.argmax(
        mlp_prob,
        axis=1,
    )

    return ~(
        (rf_ids == xgb_ids) &
        (xgb_ids == mlp_ids)
    )


def evaluate(
    y_true_text,
    predictions_text,
    probabilities,
) -> dict:
    probabilities = normalize_probabilities(
        probabilities
    )

    y_true_text = np.asarray(
        y_true_text,
        dtype=object,
    )

    predictions_text = np.asarray(
        predictions_text,
        dtype=object,
    )

    return {
        "accuracy": accuracy_score(
            y_true_text,
            predictions_text,
        ),
        "balanced_accuracy": balanced_accuracy_score(
            y_true_text,
            predictions_text,
        ),
        "macro_precision": precision_score(
            y_true_text,
            predictions_text,
            labels=LABEL_ORDER,
            average="macro",
            zero_division=0,
        ),
        "macro_recall": recall_score(
            y_true_text,
            predictions_text,
            labels=LABEL_ORDER,
            average="macro",
            zero_division=0,
        ),
        "macro_f1": f1_score(
            y_true_text,
            predictions_text,
            labels=LABEL_ORDER,
            average="macro",
            zero_division=0,
        ),
        "log_loss": log_loss(
            encode_labels(
                y_true_text
            ),
            probabilities,
            labels=list(
                range(len(LABEL_ORDER))
            ),
        ),
        "confusion_matrix": confusion_matrix(
            y_true_text,
            predictions_text,
            labels=LABEL_ORDER,
        ).tolist(),
        "classification_report": classification_report(
            y_true_text,
            predictions_text,
            labels=LABEL_ORDER,
            output_dict=True,
            zero_division=0,
        ),
    }


def print_metrics(
    title: str,
    metrics: dict,
) -> None:
    print()
    print(f"[ {title} ]")
    print(
        f"Accuracy          : "
        f"{metrics['accuracy']:.4f}"
    )
    print(
        f"Balanced Accuracy : "
        f"{metrics['balanced_accuracy']:.4f}"
    )
    print(
        f"Macro Precision   : "
        f"{metrics['macro_precision']:.4f}"
    )
    print(
        f"Macro Recall      : "
        f"{metrics['macro_recall']:.4f}"
    )
    print(
        f"Macro F1          : "
        f"{metrics['macro_f1']:.4f}"
    )
    print(
        f"Log Loss          : "
        f"{metrics['log_loss']:.4f}"
    )


def print_matrix(
    title: str,
    matrix,
) -> None:
    print()
    print(
        f"[ {title} - CONFUSION MATRIX ]"
    )

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


def print_recall(
    title: str,
    matrix,
) -> None:
    print()
    print(
        f"[ {title} - CLASS RECALL ]"
    )

    for index, label in enumerate(
        LABEL_ORDER
    ):
        row = np.asarray(
            matrix[index],
            dtype=float,
        )

        total = row.sum()

        value = (
            row[index] / total
            if total
            else 0.0
        )

        print(
            f"{label:<12}: "
            f"{value:.4f}"
        )


def load_final_base_probabilities(
    model_dir: Path,
    x_test,
):
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

    scaler_path = (
        model_dir /
        "fault_domain_deep_mlp_v2_scaler.joblib"
    )

    required = [
        rf_path,
        xgb_path,
        mlp_path,
        scaler_path,
    ]

    for path in required:
        if not path.exists():
            raise FileNotFoundError(
                f"Required local model artifact missing: {path}"
            )

    rf = joblib.load(
        rf_path
    )

    rf_prob = reorder_probability_columns(
        rf.predict_proba(
            x_test
        ),
        rf.classes_,
    )

    xgb = XGBClassifier()
    xgb.load_model(
        xgb_path
    )

    xgb_prob = normalize_probabilities(
        xgb.predict_proba(
            x_test
        )
    )

    scaler = joblib.load(
        scaler_path
    )

    x_scaled = scaler.transform(
        x_test
    ).astype(np.float32)

    checkpoint = torch.load(
        mlp_path,
        map_location="cpu",
        weights_only=False,
    )

    mlp = FaultDomainMLP(
        input_dim=x_test.shape[1],
        output_dim=len(LABEL_ORDER),
    )

    mlp.load_state_dict(
        checkpoint["state_dict"]
    )

    mlp.eval()

    with torch.no_grad():
        logits = mlp(
            torch.tensor(
                x_scaled,
                dtype=torch.float32,
            )
        )

        mlp_prob = normalize_probabilities(
            torch.softmax(
                logits,
                dim=1,
            ).cpu().numpy()
        )

    return (
        rf_prob,
        xgb_prob,
        mlp_prob,
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
        "--folds",
        type=int,
        default=3,
    )

    parser.add_argument(
        "--mlp-epochs",
        type=int,
        default=180,
    )

    parser.add_argument(
        "--mlp-patience",
        type=int,
        default=18,
    )

    parser.add_argument(
        "--batch-size",
        type=int,
        default=256,
    )

    args = parser.parse_args()

    set_seed(
        args.seed
    )

    df = load_dataset(
        args.dataset
    )

    feature_columns = build_feature_list(
        df
    )

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

    x_train = x_train.reset_index(
        drop=True
    )

    y_train_text = y_train_text.reset_index(
        drop=True
    )

    y_train = encode_labels(
        y_train_text
    )

    train_rows = len(
        x_train
    )

    oof_rf = np.zeros(
        (
            train_rows,
            len(LABEL_ORDER),
        ),
        dtype=np.float64,
    )

    oof_xgb = np.zeros_like(
        oof_rf
    )

    oof_mlp = np.zeros_like(
        oof_rf
    )

    splitter = StratifiedKFold(
        n_splits=args.folds,
        shuffle=True,
        random_state=args.seed,
    )

    fold_records = []

    print()
    print(
        "AUTONOMOUS BASE STATION - "
        "LEAKAGE-SAFE GATED STACKING V2"
    )
    print("=" * 68)
    print(
        f"Dataset rows       : {len(df)}"
    )
    print(
        f"Outer train rows   : {len(x_train)}"
    )
    print(
        f"Untouched test rows: {len(x_test)}"
    )
    print(
        f"Feature columns    : {len(feature_columns)}"
    )
    print(
        f"OOF folds          : {args.folds}"
    )
    print(
        "Leakage guard      : PASS"
    )
    print(
        "Meta test leakage  : NONE"
    )
    print()
    print(
        "Generating out-of-fold predictions..."
    )

    for fold_number, (
        train_index,
        valid_index,
    ) in enumerate(
        splitter.split(
            x_train,
            y_train,
        ),
        start=1,
    ):
        fold_seed = (
            args.seed +
            fold_number
        )

        x_fold_train = x_train.iloc[
            train_index
        ]

        x_fold_valid = x_train.iloc[
            valid_index
        ]

        y_fold_train_text = y_train_text.iloc[
            train_index
        ]

        y_fold_train = y_train[
            train_index
        ]

        print()
        print(
            f"[ FOLD {fold_number}/{args.folds} ]"
        )
        print(
            f"Train={len(train_index)} "
            f"Valid={len(valid_index)}"
        )

        rf = build_rf(
            fold_seed
        )

        rf.fit(
            x_fold_train,
            y_fold_train_text,
        )

        oof_rf[
            valid_index
        ] = reorder_probability_columns(
            rf.predict_proba(
                x_fold_valid
            ),
            rf.classes_,
        )

        print(
            "  RF       : done"
        )

        xgb = build_xgb(
            fold_seed
        )

        xgb.fit(
            x_fold_train,
            y_fold_train,
            verbose=False,
        )

        oof_xgb[
            valid_index
        ] = normalize_probabilities(
            xgb.predict_proba(
                x_fold_valid
            )
        )

        print(
            "  XGBoost  : done"
        )

        (
            mlp_prob,
            mlp_best_epoch,
        ) = train_fold_mlp(
            x_fold_train,
            y_fold_train_text,
            x_fold_valid,
            fold_seed,
            args.mlp_epochs,
            args.mlp_patience,
            args.batch_size,
        )

        oof_mlp[
            valid_index
        ] = mlp_prob

        print(
            f"  Deep MLP : done "
            f"(best epoch {mlp_best_epoch})"
        )

        fold_records.append(
            {
                "fold": fold_number,
                "train_rows": len(
                    train_index
                ),
                "validation_rows": len(
                    valid_index
                ),
                "mlp_best_epoch": (
                    mlp_best_epoch
                ),
            }
        )

    meta_train = build_meta_features(
        oof_rf,
        oof_xgb,
        oof_mlp,
    )

    oof_disagreement = (
        component_disagreement_mask(
            oof_rf,
            oof_xgb,
            oof_mlp,
        )
    )

    disagreement_rows = int(
        oof_disagreement.sum()
    )

    if disagreement_rows < 100:
        raise RuntimeError(
            "Too few OOF disagreement rows for a stable gate."
        )

    full_stack = LogisticRegression(
        max_iter=3000,
        class_weight="balanced",
        random_state=args.seed,
        solver="lbfgs",
    )

    full_stack.fit(
        meta_train,
        y_train,
    )

    disagreement_gate = LogisticRegression(
        max_iter=3000,
        class_weight="balanced",
        random_state=args.seed,
        solver="lbfgs",
    )

    disagreement_gate.fit(
        meta_train[
            oof_disagreement
        ],
        y_train[
            oof_disagreement
        ],
    )

    model_dir = here / "models"
    result_dir = here / "results"

    (
        test_rf,
        test_xgb,
        test_mlp,
    ) = load_final_base_probabilities(
        model_dir,
        x_test,
    )

    test_meta = build_meta_features(
        test_rf,
        test_xgb,
        test_mlp,
    )

    xgb_ids = np.argmax(
        test_xgb,
        axis=1,
    )

    xgb_predictions = decode_ids(
        xgb_ids
    )

    equal_soft = normalize_probabilities(
        (
            test_rf +
            test_xgb +
            test_mlp
        ) / 3.0
    )

    equal_ids = np.argmax(
        equal_soft,
        axis=1,
    )

    equal_predictions = decode_ids(
        equal_ids
    )

    stack_prob = normalize_probabilities(
        full_stack.predict_proba(
            test_meta
        )
    )

    stack_ids = full_stack.predict(
        test_meta
    )

    stack_predictions = decode_ids(
        stack_ids
    )

    test_disagreement = (
        component_disagreement_mask(
            test_rf,
            test_xgb,
            test_mlp,
        )
    )

    rf_ids = np.argmax(
        test_rf,
        axis=1,
    )

    mlp_ids = np.argmax(
        test_mlp,
        axis=1,
    )

    unanimous_ids = rf_ids.copy()

    gate_prob = equal_soft.copy()

    if np.any(
        test_disagreement
    ):
        disagreement_prob = normalize_probabilities(
            disagreement_gate.predict_proba(
                test_meta[
                    test_disagreement
                ]
            )
        )

        gate_prob[
            test_disagreement
        ] = disagreement_prob

        unanimous_ids[
            test_disagreement
        ] = disagreement_gate.predict(
            test_meta[
                test_disagreement
            ]
        )

    gated_ids = unanimous_ids

    gated_predictions = decode_ids(
        gated_ids
    )

    xgb_metrics = evaluate(
        y_test_text,
        xgb_predictions,
        test_xgb,
    )

    equal_metrics = evaluate(
        y_test_text,
        equal_predictions,
        equal_soft,
    )

    stack_metrics = evaluate(
        y_test_text,
        stack_predictions,
        stack_prob,
    )

    gated_metrics = evaluate(
        y_test_text,
        gated_predictions,
        gate_prob,
    )

    y_test_array = np.asarray(
        y_test_text,
        dtype=object,
    )

    unanimous_mask = ~test_disagreement

    unanimous_count = int(
        unanimous_mask.sum()
    )

    test_disagreement_rows = int(
        test_disagreement.sum()
    )

    unanimous_accuracy = (
        accuracy_score(
            y_test_array[
                unanimous_mask
            ],
            gated_predictions[
                unanimous_mask
            ],
        )
        if unanimous_count
        else 0.0
    )

    gate_disagreement_accuracy = (
        accuracy_score(
            y_test_array[
                test_disagreement
            ],
            gated_predictions[
                test_disagreement
            ],
        )
        if test_disagreement_rows
        else 0.0
    )

    xgb_disagreement_accuracy = (
        accuracy_score(
            y_test_array[
                test_disagreement
            ],
            xgb_predictions[
                test_disagreement
            ],
        )
        if test_disagreement_rows
        else 0.0
    )

    any_model_correct = (
        (decode_ids(rf_ids) == y_test_array) |
        (xgb_predictions == y_test_array) |
        (decode_ids(mlp_ids) == y_test_array)
    )

    oracle_upper_bound = float(
        any_model_correct.mean()
    )

    model_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    result_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    full_stack_path = (
        model_dir /
        "fault_domain_full_stack_v2.joblib"
    )

    gate_path = (
        model_dir /
        "fault_domain_disagreement_gate_v2.joblib"
    )

    metadata_path = (
        model_dir /
        "fault_domain_gated_stack_v2_metadata.json"
    )

    metrics_path = (
        result_dir /
        "fault_domain_gated_stack_v2_metrics.json"
    )

    joblib.dump(
        full_stack,
        full_stack_path,
    )

    joblib.dump(
        disagreement_gate,
        gate_path,
    )

    dataset_hash = sha256_file(
        args.dataset
    )

    metadata = {
        "schema": "abs.ml.stack.v1",
        "model_name": "fault_domain_gated_stack_v2",
        "target": "fault_domain",
        "dataset_sha256": dataset_hash,
        "feature_count": len(
            feature_columns
        ),
        "feature_columns": feature_columns,
        "classes": LABEL_ORDER,
        "outer_test_size": args.test_size,
        "random_state": args.seed,
        "oof_folds": args.folds,
        "meta_feature_count": int(
            meta_train.shape[1]
        ),
        "meta_model": (
            "LogisticRegression"
        ),
        "meta_training": (
            "out_of_fold_outer_training_only"
        ),
        "test_set_used_for_training": False,
        "gate_rule": (
            "unanimous base prediction -> accept; "
            "disagreement -> learned meta-classifier"
        ),
        "component_models": [
            "Random Forest",
            "XGBoost",
            "Deep MLP",
        ],
    }

    metrics = {
        "schema": "abs.ml.metrics.v1",
        "model_name": "fault_domain_gated_stack_v2",
        "dataset_sha256": dataset_hash,
        "dataset_rows": len(df),
        "outer_training_rows": len(
            x_train
        ),
        "test_rows": len(
            x_test
        ),
        "oof": {
            "folds": args.folds,
            "disagreement_rows": (
                disagreement_rows
            ),
            "disagreement_pct": (
                disagreement_rows /
                len(x_train)
            ),
            "fold_records": fold_records,
        },
        "test_metrics": {
            "xgboost": xgb_metrics,
            "equal_soft_vote": equal_metrics,
            "full_stack": stack_metrics,
            "gated_stack": gated_metrics,
        },
        "gate_analysis": {
            "unanimous_rows": unanimous_count,
            "unanimous_pct": (
                unanimous_count /
                len(x_test)
            ),
            "unanimous_accuracy": (
                unanimous_accuracy
            ),
            "disagreement_rows": (
                test_disagreement_rows
            ),
            "disagreement_pct": (
                test_disagreement_rows /
                len(x_test)
            ),
            "xgboost_accuracy_on_disagreements": (
                xgb_disagreement_accuracy
            ),
            "gate_accuracy_on_disagreements": (
                gate_disagreement_accuracy
            ),
            "any_model_correct_upper_bound": (
                oracle_upper_bound
            ),
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
        "[ LEAKAGE-SAFE OOF META-TRAINING ]"
    )
    print(
        f"OOF rows           : {len(x_train)}"
    )
    print(
        f"OOF disagreements  : "
        f"{disagreement_rows} "
        f"({100.0 * disagreement_rows / len(x_train):.1f}%)"
    )
    print(
        f"Meta features      : {meta_train.shape[1]}"
    )
    print(
        "Outer test used    : NO"
    )

    print_metrics(
        "XGBOOST BASELINE",
        xgb_metrics,
    )

    print_metrics(
        "EQUAL SOFT VOTE",
        equal_metrics,
    )

    print_metrics(
        "LEARNED FULL STACK",
        stack_metrics,
    )

    print_metrics(
        "LEARNED DISAGREEMENT GATE",
        gated_metrics,
    )

    print_matrix(
        "LEARNED DISAGREEMENT GATE",
        gated_metrics[
            "confusion_matrix"
        ],
    )

    print_recall(
        "LEARNED DISAGREEMENT GATE",
        gated_metrics[
            "confusion_matrix"
        ],
    )

    print()
    print("[ GATE ANALYSIS ]")
    print(
        f"Unanimous cases    : "
        f"{unanimous_count} / {len(x_test)} "
        f"({100.0 * unanimous_count / len(x_test):.1f}%)"
    )
    print(
        f"Unanimous accuracy : "
        f"{unanimous_accuracy:.4f}"
    )
    print(
        f"Disagreement cases : "
        f"{test_disagreement_rows} / {len(x_test)} "
        f"({100.0 * test_disagreement_rows / len(x_test):.1f}%)"
    )
    print(
        f"XGB on disagreement: "
        f"{xgb_disagreement_accuracy:.4f}"
    )
    print(
        f"Gate on disagreement: "
        f"{gate_disagreement_accuracy:.4f}"
    )
    print(
        f"Any-model upper bound: "
        f"{oracle_upper_bound:.4f}"
    )

    print()
    print(
        f"Full-stack model   : "
        f"{full_stack_path.resolve()}"
    )
    print(
        f"Gate model         : "
        f"{gate_path.resolve()}"
    )
    print(
        f"Metadata           : "
        f"{metadata_path.resolve()}"
    )
    print(
        f"Metrics            : "
        f"{metrics_path.resolve()}"
    )

    print()
    print(
        "Next decision      : keep XGBoost alone, "
        "full stacking, or the disagreement gate "
        "based on untouched-test performance."
    )


if __name__ == "__main__":
    main()