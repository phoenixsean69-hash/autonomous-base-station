#!/usr/bin/env python3
"""
Hierarchical temporal root-cause diagnosis for the Autonomous Base Station.

Stage 1 already predicts:
    NORMAL / LOCAL / UPSTREAM / MIXED

This stage adds two independent temporal heads:

LOCAL head
    COOLING_FAULT
    RADIO_FAULT
    RECTIFIER_FAULT
    RF_MISMATCH
    BATTERY_LOW
    GRID_FAILURE
    MECHANICAL_VIBRATION
    TRAFFIC_OVERLOAD

UPSTREAM head
    BACKHAUL_CONGESTION
    UPSTREAM_LINK_DEGRADATION
    UPSTREAM_LINK_FAILURE
    UPSTREAM_OUTAGE

For MIXED sequences, BOTH heads are evaluated.

Evaluation design
-----------------
- uses the exact same deterministic outer 80/20 sequence split as the V3 TCN
- outer test set is untouched during training
- each head uses only sequences where that root cause is defined
- inner 15% validation split selects the best epoch
- final model is retrained on the full eligible outer-training partition
- temporal TCN is compared with a final-frame XGBoost baseline
- MIXED exact-pair accuracy is reported separately

Important:
This stage evaluates root-cause heads conditioned on the sequence actually
having a local and/or upstream root cause. End-to-end domain gating is added
later in the unified runtime.
"""

from __future__ import annotations

import argparse
import json
import random
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
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from torch import nn
from torch.utils.data import DataLoader, TensorDataset
from xgboost import XGBClassifier

import generate_dataset as v1
from ai_common import LABEL_ORDER, sha256_file
from train_temporal_tcn import FaultDomainTCN


DOMAIN_TO_ID = {
    label: index
    for index, label in enumerate(LABEL_ORDER)
}


def set_deterministic(seed: int) -> None:
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)

    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)

    try:
        torch.use_deterministic_algorithms(
            True,
            warn_only=True,
        )
    except Exception:
        pass


def fit_sequence_scaler(
    x: np.ndarray,
) -> StandardScaler:
    scaler = StandardScaler()

    scaler.fit(
        x.reshape(
            -1,
            x.shape[-1],
        )
    )

    return scaler


def transform_sequences(
    scaler: StandardScaler,
    x: np.ndarray,
) -> np.ndarray:
    transformed = scaler.transform(
        x.reshape(
            -1,
            x.shape[-1],
        )
    )

    return transformed.reshape(
        x.shape
    ).astype(
        np.float32
    )


def make_loader(
    x: np.ndarray,
    y: np.ndarray,
    batch_size: int,
    shuffle: bool,
    seed: int,
) -> DataLoader:
    dataset = TensorDataset(
        torch.tensor(
            x,
            dtype=torch.float32,
        ),
        torch.tensor(
            y,
            dtype=torch.long,
        ),
    )

    generator = torch.Generator()
    generator.manual_seed(seed)

    return DataLoader(
        dataset,
        batch_size=batch_size,
        shuffle=shuffle,
        generator=(
            generator
            if shuffle
            else None
        ),
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


def run_training_epoch(
    model: nn.Module,
    loader: DataLoader,
    criterion,
    optimizer,
    device,
) -> float:
    model.train()

    total_loss = 0.0
    total_items = 0

    for features, labels in loader:
        features = features.to(
            device
        )

        labels = labels.to(
            device
        )

        optimizer.zero_grad(
            set_to_none=True
        )

        logits = model(
            features
        )

        loss = criterion(
            logits,
            labels,
        )

        loss.backward()

        torch.nn.utils.clip_grad_norm_(
            model.parameters(),
            max_norm=5.0,
        )

        optimizer.step()

        count = labels.size(0)

        total_loss += (
            float(loss.item()) *
            count
        )

        total_items += count

    return (
        total_loss /
        total_items
    )


def evaluation_loss(
    model: nn.Module,
    loader: DataLoader,
    criterion,
    device,
) -> float:
    model.eval()

    total_loss = 0.0
    total_items = 0

    with torch.no_grad():
        for features, labels in loader:
            features = features.to(
                device
            )

            labels = labels.to(
                device
            )

            logits = model(
                features
            )

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

    return (
        total_loss /
        total_items
    )


def choose_best_epoch(
    x_fit: np.ndarray,
    y_fit: np.ndarray,
    x_validation: np.ndarray,
    y_validation: np.ndarray,
    class_count: int,
    batch_size: int,
    learning_rate: float,
    max_epochs: int,
    patience: int,
    seed: int,
    device,
):
    set_deterministic(
        seed
    )

    model = FaultDomainTCN(
        feature_count=x_fit.shape[-1],
        class_count=class_count,
    ).to(device)

    criterion = nn.CrossEntropyLoss()

    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=learning_rate,
        weight_decay=1e-4,
    )

    fit_loader = make_loader(
        x_fit,
        y_fit,
        batch_size,
        True,
        seed,
    )

    validation_loader = make_loader(
        x_validation,
        y_validation,
        batch_size,
        False,
        seed,
    )

    best_epoch = 0
    best_loss = float(
        "inf"
    )

    stale = 0
    history = []

    for epoch in range(
        1,
        max_epochs + 1,
    ):
        train_loss = run_training_epoch(
            model,
            fit_loader,
            criterion,
            optimizer,
            device,
        )

        validation_loss = evaluation_loss(
            model,
            validation_loader,
            criterion,
            device,
        )

        history.append(
            {
                "epoch": epoch,
                "train_loss": train_loss,
                "validation_loss": validation_loss,
            }
        )

        improved = (
            validation_loss <
            best_loss - 1e-4
        )

        if improved:
            best_loss = validation_loss
            best_epoch = epoch
            stale = 0
        else:
            stale += 1

        if (
            epoch == 1 or
            epoch % 5 == 0 or
            improved
        ):
            print(
                f"Epoch {epoch:03d} | "
                f"train={train_loss:.4f} | "
                f"val={validation_loss:.4f}"
            )

        if stale >= patience:
            print(
                f"Early stopping after epoch {epoch}; "
                f"best epoch={best_epoch}."
            )
            break

    if best_epoch <= 0:
        raise RuntimeError(
            "Could not select a valid epoch."
        )

    return (
        best_epoch,
        best_loss,
        history,
    )


def train_final_model(
    x_train: np.ndarray,
    y_train: np.ndarray,
    class_count: int,
    batch_size: int,
    learning_rate: float,
    epochs: int,
    seed: int,
    device,
):
    set_deterministic(
        seed
    )

    model = FaultDomainTCN(
        feature_count=x_train.shape[-1],
        class_count=class_count,
    ).to(device)

    criterion = nn.CrossEntropyLoss()

    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=learning_rate,
        weight_decay=1e-4,
    )

    loader = make_loader(
        x_train,
        y_train,
        batch_size,
        True,
        seed,
    )

    history = []

    for epoch in range(
        1,
        epochs + 1,
    ):
        loss = run_training_epoch(
            model,
            loader,
            criterion,
            optimizer,
            device,
        )

        history.append(
            {
                "epoch": epoch,
                "train_loss": loss,
            }
        )

        if (
            epoch == 1 or
            epoch % 5 == 0 or
            epoch == epochs
        ):
            print(
                f"Final train epoch "
                f"{epoch:03d}/{epochs:03d} | "
                f"loss={loss:.4f}"
            )

    return (
        model,
        history,
    )


def predict_tcn(
    model: nn.Module,
    x: np.ndarray,
    batch_size: int,
    device,
) -> np.ndarray:
    dummy = np.zeros(
        len(x),
        dtype=np.int64,
    )

    loader = make_loader(
        x,
        dummy,
        batch_size,
        False,
        seed=1,
    )

    outputs = []

    model.eval()

    with torch.no_grad():
        for features, _ in loader:
            features = features.to(
                device
            )

            logits = model(
                features
            )

            probabilities = torch.softmax(
                logits,
                dim=1,
            )

            outputs.append(
                probabilities
                .cpu()
                .numpy()
            )

    return normalize_probabilities(
        np.vstack(
            outputs
        )
    )


def build_xgb(
    class_count: int,
    seed: int,
) -> XGBClassifier:
    return XGBClassifier(
        objective="multi:softprob",
        num_class=class_count,
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


def calculate_metrics(
    y_true: np.ndarray,
    probabilities: np.ndarray,
    class_names: list[str],
) -> dict:
    predictions = np.argmax(
        probabilities,
        axis=1,
    )

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
            labels=list(
                range(
                    len(class_names)
                )
            ),
            average="macro",
            zero_division=0,
        ),
        "macro_recall": recall_score(
            y_true,
            predictions,
            labels=list(
                range(
                    len(class_names)
                )
            ),
            average="macro",
            zero_division=0,
        ),
        "macro_f1": f1_score(
            y_true,
            predictions,
            labels=list(
                range(
                    len(class_names)
                )
            ),
            average="macro",
            zero_division=0,
        ),
        "log_loss": log_loss(
            y_true,
            probabilities,
            labels=list(
                range(
                    len(class_names)
                )
            ),
        ),
        "confusion_matrix": confusion_matrix(
            y_true,
            predictions,
            labels=list(
                range(
                    len(class_names)
                )
            ),
        ).tolist(),
        "classification_report": classification_report(
            y_true,
            predictions,
            labels=list(
                range(
                    len(class_names)
                )
            ),
            target_names=class_names,
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


def print_confusion(
    title: str,
    matrix,
    class_names: list[str],
) -> None:
    print()
    print(
        f"[ {title} - CONFUSION MATRIX ]"
    )

    print(
        "Actual \\ Pred"
    )

    header = " " * 25

    for index in range(
        len(class_names)
    ):
        header += f"{index:>6}"

    print(header)

    for index, row in enumerate(
        matrix
    ):
        label = class_names[index]

        print(
            f"{index:>2} {label[:20]:<20} " +
            " ".join(
                f"{int(value):>5}"
                for value in row
            )
        )

    print()
    print("Class index legend:")

    for index, label in enumerate(
        class_names
    ):
        print(
            f"  {index}: {label}"
        )


def print_class_recall(
    title: str,
    matrix,
    class_names: list[str],
) -> None:
    print()
    print(
        f"[ {title} - CLASS RECALL ]"
    )

    for index, label in enumerate(
        class_names
    ):
        row = np.asarray(
            matrix[index],
            dtype=float,
        )

        total = row.sum()

        recall = (
            row[index] / total
            if total
            else 0.0
        )

        print(
            f"{label:<28}: "
            f"{recall:.4f}"
        )


def encode_labels(
    values: np.ndarray,
    class_names: list[str],
) -> np.ndarray:
    mapping = {
        label: index
        for index, label
        in enumerate(
            class_names
        )
    }

    missing = sorted(
        set(
            values.tolist()
        ) -
        set(
            class_names
        )
    )

    if missing:
        raise RuntimeError(
            "Unexpected root-cause labels: " +
            ", ".join(
                missing
            )
        )

    return np.asarray(
        [
            mapping[
                str(value)
            ]
            for value in values
        ],
        dtype=np.int64,
    )


def train_head(
    *,
    head_name: str,
    class_names: list[str],
    label_array: np.ndarray,
    x: np.ndarray,
    outer_train_indices: np.ndarray,
    test_indices: np.ndarray,
    validation_size: float,
    batch_size: int,
    learning_rate: float,
    max_epochs: int,
    patience: int,
    seed: int,
    device,
    model_dir: Path,
):
    eligible_train = np.asarray(
        [
            index
            for index in outer_train_indices
            if str(
                label_array[index]
            ) != "NONE"
        ],
        dtype=np.int64,
    )

    eligible_test = np.asarray(
        [
            index
            for index in test_indices
            if str(
                label_array[index]
            ) != "NONE"
        ],
        dtype=np.int64,
    )

    if len(
        eligible_train
    ) == 0:
        raise RuntimeError(
            f"No training rows available for {head_name}."
        )

    y_train_all = encode_labels(
        label_array[
            eligible_train
        ],
        class_names,
    )

    y_test = encode_labels(
        label_array[
            eligible_test
        ],
        class_names,
    )

    (
        fit_indices,
        validation_indices,
    ) = train_test_split(
        eligible_train,
        test_size=validation_size,
        random_state=seed,
        stratify=y_train_all,
    )

    y_fit = encode_labels(
        label_array[
            fit_indices
        ],
        class_names,
    )

    y_validation = encode_labels(
        label_array[
            validation_indices
        ],
        class_names,
    )

    selection_scaler = fit_sequence_scaler(
        x[
            fit_indices
        ]
    )

    x_fit = transform_sequences(
        selection_scaler,
        x[
            fit_indices
        ],
    )

    x_validation = transform_sequences(
        selection_scaler,
        x[
            validation_indices
        ],
    )

    print()
    print(
        f"[ {head_name} ROOT-CAUSE HEAD ]"
    )
    print(
        f"Classes             : "
        f"{len(class_names)}"
    )
    print(
        f"Eligible train rows : "
        f"{len(eligible_train)}"
    )
    print(
        f"Fit rows            : "
        f"{len(fit_indices)}"
    )
    print(
        f"Validation rows     : "
        f"{len(validation_indices)}"
    )
    print(
        f"Eligible test rows  : "
        f"{len(eligible_test)}"
    )

    print()
    print(
        f"[ {head_name} - PHASE 1 EPOCH SELECTION ]"
    )

    (
        best_epoch,
        best_validation_loss,
        selection_history,
    ) = choose_best_epoch(
        x_fit=x_fit,
        y_fit=y_fit,
        x_validation=x_validation,
        y_validation=y_validation,
        class_count=len(
            class_names
        ),
        batch_size=batch_size,
        learning_rate=learning_rate,
        max_epochs=max_epochs,
        patience=patience,
        seed=seed,
        device=device,
    )

    print()
    print(
        f"{head_name} selected epoch      : "
        f"{best_epoch}"
    )
    print(
        f"{head_name} best validation loss: "
        f"{best_validation_loss:.4f}"
    )

    final_scaler = fit_sequence_scaler(
        x[
            eligible_train
        ]
    )

    x_train_scaled = transform_sequences(
        final_scaler,
        x[
            eligible_train
        ],
    )

    x_test_scaled = transform_sequences(
        final_scaler,
        x[
            eligible_test
        ],
    )

    print()
    print(
        f"[ {head_name} - PHASE 2 FINAL TCN ]"
    )

    (
        model,
        final_history,
    ) = train_final_model(
        x_train=x_train_scaled,
        y_train=y_train_all,
        class_count=len(
            class_names
        ),
        batch_size=batch_size,
        learning_rate=learning_rate,
        epochs=best_epoch,
        seed=seed + 1000,
        device=device,
    )

    tcn_probabilities = predict_tcn(
        model,
        x_test_scaled,
        batch_size,
        device,
    )

    tcn_metrics = calculate_metrics(
        y_test,
        tcn_probabilities,
        class_names,
    )

    print()
    print(
        f"[ {head_name} - FINAL-FRAME XGBOOST BASELINE ]"
    )

    xgb = build_xgb(
        len(
            class_names
        ),
        seed,
    )

    xgb.fit(
        x[
            eligible_train,
            -1,
            :,
        ],
        y_train_all,
        verbose=False,
    )

    xgb_probabilities = normalize_probabilities(
        xgb.predict_proba(
            x[
                eligible_test,
                -1,
                :,
            ]
        )
    )

    xgb_metrics = calculate_metrics(
        y_test,
        xgb_probabilities,
        class_names,
    )

    print_metrics(
        f"{head_name} FINAL-FRAME XGBOOST",
        xgb_metrics,
    )

    print_metrics(
        f"{head_name} TEMPORAL TCN",
        tcn_metrics,
    )

    print_confusion(
        f"{head_name} TEMPORAL TCN",
        tcn_metrics[
            "confusion_matrix"
        ],
        class_names,
    )

    print_class_recall(
        f"{head_name} TEMPORAL TCN",
        tcn_metrics[
            "confusion_matrix"
        ],
        class_names,
    )

    print()
    print(
        f"[ {head_name} TEMPORAL VALUE CHECK ]"
    )

    print(
        f"Accuracy delta      : "
        f"{tcn_metrics['accuracy'] - xgb_metrics['accuracy']:+.4f}"
    )

    print(
        f"Macro-F1 delta      : "
        f"{tcn_metrics['macro_f1'] - xgb_metrics['macro_f1']:+.4f}"
    )

    model_path = (
        model_dir /
        (
            "local_root_cause_tcn_v3.pt"
            if head_name == "LOCAL"
            else "upstream_root_cause_tcn_v3.pt"
        )
    )

    scaler_path = (
        model_dir /
        (
            "local_root_cause_tcn_v3_scaler.joblib"
            if head_name == "LOCAL"
            else "upstream_root_cause_tcn_v3_scaler.joblib"
        )
    )

    torch.save(
        {
            "state_dict": {
                key: value.detach().cpu()
                for key, value
                in model.state_dict().items()
            },
            "feature_count": int(
                x.shape[-1]
            ),
            "class_count": len(
                class_names
            ),
            "classes": class_names,
            "architecture": (
                "causal_tcn_64_dilations_1_2_4"
            ),
        },
        model_path,
    )

    joblib.dump(
        final_scaler,
        scaler_path,
    )

    predictions = np.argmax(
        tcn_probabilities,
        axis=1,
    )

    prediction_by_global_index = {
        int(
            global_index
        ): class_names[
            int(
                prediction
            )
        ]
        for global_index, prediction
        in zip(
            eligible_test,
            predictions,
        )
    }

    return {
        "head_name": head_name,
        "classes": class_names,
        "eligible_train_indices": (
            eligible_train
        ),
        "eligible_test_indices": (
            eligible_test
        ),
        "fit_indices": (
            fit_indices
        ),
        "validation_indices": (
            validation_indices
        ),
        "best_epoch": best_epoch,
        "best_validation_loss": (
            best_validation_loss
        ),
        "selection_history": (
            selection_history
        ),
        "final_training_history": (
            final_history
        ),
        "xgboost_metrics": (
            xgb_metrics
        ),
        "tcn_metrics": (
            tcn_metrics
        ),
        "prediction_by_global_index": (
            prediction_by_global_index
        ),
        "model_path": model_path,
        "scaler_path": scaler_path,
    }


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
        "--test-size",
        type=float,
        default=0.20,
    )

    parser.add_argument(
        "--validation-size",
        type=float,
        default=0.15,
    )

    parser.add_argument(
        "--seed",
        type=int,
        default=42,
    )

    parser.add_argument(
        "--epochs",
        type=int,
        default=120,
    )

    parser.add_argument(
        "--patience",
        type=int,
        default=15,
    )

    parser.add_argument(
        "--batch-size",
        type=int,
        default=256,
    )

    parser.add_argument(
        "--learning-rate",
        type=float,
        default=0.001,
    )

    args = parser.parse_args()

    if not args.dataset.exists():
        raise FileNotFoundError(
            f"Temporal dataset not found: {args.dataset}"
        )

    set_deterministic(
        args.seed
    )

    with np.load(
        args.dataset,
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

        local_root_cause = dataset[
            "local_root_cause"
        ].astype(
            str
        )

        upstream_root_cause = dataset[
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

        label_order = [
            str(value)
            for value in dataset[
                "label_order"
            ].tolist()
        ]

    if label_order != LABEL_ORDER:
        raise RuntimeError(
            "V3 label order does not match ai_common.LABEL_ORDER."
        )

    if not np.isfinite(
        x
    ).all():
        raise RuntimeError(
            "Temporal dataset contains NaN or infinite values."
        )

    indices = np.arange(
        len(y_domain)
    )

    (
        outer_train_indices,
        test_indices,
    ) = train_test_split(
        indices,
        test_size=args.test_size,
        random_state=args.seed,
        stratify=y_domain,
    )

    device = torch.device(
        "cuda"
        if torch.cuda.is_available()
        else "cpu"
    )

    print()
    print(
        "AUTONOMOUS BASE STATION - "
        "HIERARCHICAL TEMPORAL ROOT-CAUSE V3"
    )
    print("=" * 72)
    print(
        f"Sequences           : {len(x)}"
    )
    print(
        f"Timesteps           : {x.shape[1]}"
    )
    print(
        f"Feature columns     : {x.shape[2]}"
    )
    print(
        f"Outer train rows    : "
        f"{len(outer_train_indices)}"
    )
    print(
        f"Untouched test rows : "
        f"{len(test_indices)}"
    )
    print(
        f"Device              : {device}"
    )
    print(
        "Outer split         : SAME AS TEMPORAL TCN V3"
    )
    print(
        "Outer test leakage  : NONE"
    )
    print(
        "MIXED handling      : BOTH ROOT-CAUSE HEADS"
    )

    model_dir = (
        here /
        "models"
    )

    result_dir = (
        here /
        "results"
    )

    model_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    result_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    local_result = train_head(
        head_name="LOCAL",
        class_names=list(
            v1.LOCAL_CAUSES
        ),
        label_array=local_root_cause,
        x=x,
        outer_train_indices=outer_train_indices,
        test_indices=test_indices,
        validation_size=args.validation_size,
        batch_size=args.batch_size,
        learning_rate=args.learning_rate,
        max_epochs=args.epochs,
        patience=args.patience,
        seed=args.seed,
        device=device,
        model_dir=model_dir,
    )

    upstream_result = train_head(
        head_name="UPSTREAM",
        class_names=list(
            v1.UPSTREAM_CAUSES
        ),
        label_array=upstream_root_cause,
        x=x,
        outer_train_indices=outer_train_indices,
        test_indices=test_indices,
        validation_size=args.validation_size,
        batch_size=args.batch_size,
        learning_rate=args.learning_rate,
        max_epochs=args.epochs,
        patience=args.patience,
        seed=args.seed + 100,
        device=device,
        model_dir=model_dir,
    )

    mixed_id = DOMAIN_TO_ID[
        "MIXED"
    ]

    mixed_test_indices = np.asarray(
        [
            index
            for index in test_indices
            if y_domain[index] == mixed_id
        ],
        dtype=np.int64,
    )

    local_correct = []
    upstream_correct = []
    both_correct = []

    for index in mixed_test_indices:
        local_prediction = (
            local_result[
                "prediction_by_global_index"
            ][
                int(index)
            ]
        )

        upstream_prediction = (
            upstream_result[
                "prediction_by_global_index"
            ][
                int(index)
            ]
        )

        local_is_correct = (
            local_prediction ==
            str(
                local_root_cause[
                    index
                ]
            )
        )

        upstream_is_correct = (
            upstream_prediction ==
            str(
                upstream_root_cause[
                    index
                ]
            )
        )

        local_correct.append(
            local_is_correct
        )

        upstream_correct.append(
            upstream_is_correct
        )

        both_correct.append(
            (
                local_is_correct and
                upstream_is_correct
            )
        )

    mixed_local_accuracy = float(
        np.mean(
            local_correct
        )
    )

    mixed_upstream_accuracy = float(
        np.mean(
            upstream_correct
        )
    )

    mixed_exact_pair_accuracy = float(
        np.mean(
            both_correct
        )
    )

    print()
    print(
        "[ MIXED ROOT-CAUSE PAIR EVALUATION ]"
    )
    print(
        f"MIXED test rows       : "
        f"{len(mixed_test_indices)}"
    )
    print(
        f"Local cause accuracy  : "
        f"{mixed_local_accuracy:.4f}"
    )
    print(
        f"Upstream accuracy     : "
        f"{mixed_upstream_accuracy:.4f}"
    )
    print(
        f"Exact BOTH causes     : "
        f"{mixed_exact_pair_accuracy:.4f}"
    )

    dataset_hash = sha256_file(
        args.dataset
    )

    metadata_path = (
        model_dir /
        "hierarchical_root_cause_v3_metadata.json"
    )

    metrics_path = (
        result_dir /
        "hierarchical_root_cause_v3_metrics.json"
    )

    metadata = {
        "schema": "abs.ml.root_cause.v1",
        "model_name": "hierarchical_root_cause_v3",
        "dataset_sha256": dataset_hash,
        "feature_columns": feature_names,
        "feature_count": len(
            feature_names
        ),
        "timesteps": int(
            x.shape[1]
        ),
        "outer_test_size": (
            args.test_size
        ),
        "inner_validation_size": (
            args.validation_size
        ),
        "random_state": (
            args.seed
        ),
        "outer_test_used_for_training": False,
        "hierarchy": {
            "fault_domain_stage": [
                "NORMAL",
                "LOCAL",
                "UPSTREAM",
                "MIXED",
            ],
            "local_head_runs_for": [
                "LOCAL",
                "MIXED",
            ],
            "upstream_head_runs_for": [
                "UPSTREAM",
                "MIXED",
            ],
            "mixed_runs_both_heads": True,
        },
        "local_head": {
            "classes": list(
                v1.LOCAL_CAUSES
            ),
            "model_type": (
                "PyTorchTCN"
            ),
            "selected_epoch": (
                local_result[
                    "best_epoch"
                ]
            ),
            "best_validation_loss": (
                local_result[
                    "best_validation_loss"
                ]
            ),
        },
        "upstream_head": {
            "classes": list(
                v1.UPSTREAM_CAUSES
            ),
            "model_type": (
                "PyTorchTCN"
            ),
            "selected_epoch": (
                upstream_result[
                    "best_epoch"
                ]
            ),
            "best_validation_loss": (
                upstream_result[
                    "best_validation_loss"
                ]
            ),
        },
        "comparison_baseline": (
            "XGBoost using only final timestep "
            "for each eligible root-cause head"
        ),
        "conditioned_evaluation": True,
        "conditioned_evaluation_note": (
            "Root-cause metrics assume the sequence is routed "
            "to the correct head. End-to-end domain gating is "
            "evaluated in the unified inference stage."
        ),
    }

    metrics = {
        "schema": "abs.ml.metrics.v1",
        "model_name": (
            "hierarchical_root_cause_v3"
        ),
        "dataset_sha256": dataset_hash,
        "dataset_sequences": int(
            len(x)
        ),
        "outer_training_rows": int(
            len(outer_train_indices)
        ),
        "test_rows": int(
            len(test_indices)
        ),
        "local_head": {
            "classes": (
                local_result[
                    "classes"
                ]
            ),
            "eligible_training_rows": int(
                len(
                    local_result[
                        "eligible_train_indices"
                    ]
                )
            ),
            "eligible_test_rows": int(
                len(
                    local_result[
                        "eligible_test_indices"
                    ]
                )
            ),
            "fit_rows": int(
                len(
                    local_result[
                        "fit_indices"
                    ]
                )
            ),
            "validation_rows": int(
                len(
                    local_result[
                        "validation_indices"
                    ]
                )
            ),
            "selected_epoch": (
                local_result[
                    "best_epoch"
                ]
            ),
            "best_validation_loss": (
                local_result[
                    "best_validation_loss"
                ]
            ),
            "selection_history": (
                local_result[
                    "selection_history"
                ]
            ),
            "final_training_history": (
                local_result[
                    "final_training_history"
                ]
            ),
            "snapshot_xgboost": (
                local_result[
                    "xgboost_metrics"
                ]
            ),
            "temporal_tcn": (
                local_result[
                    "tcn_metrics"
                ]
            ),
            "temporal_value_check": {
                "accuracy_delta": (
                    local_result[
                        "tcn_metrics"
                    ][
                        "accuracy"
                    ] -
                    local_result[
                        "xgboost_metrics"
                    ][
                        "accuracy"
                    ]
                ),
                "macro_f1_delta": (
                    local_result[
                        "tcn_metrics"
                    ][
                        "macro_f1"
                    ] -
                    local_result[
                        "xgboost_metrics"
                    ][
                        "macro_f1"
                    ]
                ),
            },
        },
        "upstream_head": {
            "classes": (
                upstream_result[
                    "classes"
                ]
            ),
            "eligible_training_rows": int(
                len(
                    upstream_result[
                        "eligible_train_indices"
                    ]
                )
            ),
            "eligible_test_rows": int(
                len(
                    upstream_result[
                        "eligible_test_indices"
                    ]
                )
            ),
            "fit_rows": int(
                len(
                    upstream_result[
                        "fit_indices"
                    ]
                )
            ),
            "validation_rows": int(
                len(
                    upstream_result[
                        "validation_indices"
                    ]
                )
            ),
            "selected_epoch": (
                upstream_result[
                    "best_epoch"
                ]
            ),
            "best_validation_loss": (
                upstream_result[
                    "best_validation_loss"
                ]
            ),
            "selection_history": (
                upstream_result[
                    "selection_history"
                ]
            ),
            "final_training_history": (
                upstream_result[
                    "final_training_history"
                ]
            ),
            "snapshot_xgboost": (
                upstream_result[
                    "xgboost_metrics"
                ]
            ),
            "temporal_tcn": (
                upstream_result[
                    "tcn_metrics"
                ]
            ),
            "temporal_value_check": {
                "accuracy_delta": (
                    upstream_result[
                        "tcn_metrics"
                    ][
                        "accuracy"
                    ] -
                    upstream_result[
                        "xgboost_metrics"
                    ][
                        "accuracy"
                    ]
                ),
                "macro_f1_delta": (
                    upstream_result[
                        "tcn_metrics"
                    ][
                        "macro_f1"
                    ] -
                    upstream_result[
                        "xgboost_metrics"
                    ][
                        "macro_f1"
                    ]
                ),
            },
        },
        "mixed_pair_evaluation": {
            "mixed_test_rows": int(
                len(
                    mixed_test_indices
                )
            ),
            "local_root_cause_accuracy": (
                mixed_local_accuracy
            ),
            "upstream_root_cause_accuracy": (
                mixed_upstream_accuracy
            ),
            "exact_both_root_causes_accuracy": (
                mixed_exact_pair_accuracy
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
        f"Local model          : "
        f"{local_result['model_path'].resolve()}"
    )
    print(
        f"Local scaler         : "
        f"{local_result['scaler_path'].resolve()}"
    )
    print(
        f"Upstream model       : "
        f"{upstream_result['model_path'].resolve()}"
    )
    print(
        f"Upstream scaler      : "
        f"{upstream_result['scaler_path'].resolve()}"
    )
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
        "Next stage           : build the unified AI inference "
        "engine that combines domain TCN, anomaly score, "
        "root causes, confidence and energy recommendation."
    )


if __name__ == "__main__":
    main()