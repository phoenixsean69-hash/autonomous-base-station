#!/usr/bin/env python3
"""
Temporal Convolutional Network (TCN) for Autonomous Base Station fault diagnosis.

Dataset:
    V3 temporal dataset: [sequence, timestep, feature]
    Default shape: 12000 x 24 x 33

Target:
    NORMAL / LOCAL / UPSTREAM / MIXED

Evaluation design:
- deterministic stratified outer 80/20 train/test split
- untouched outer test set
- inner 15% validation split used ONLY to select the best TCN epoch
- after epoch selection, the TCN is retrained from scratch on the full
  outer-training partition for exactly the selected number of epochs
- feature scaling is fitted on training data only
- final-frame XGBoost is trained on the SAME outer-training sequences and
  evaluated on the SAME untouched test sequences

This gives a meaningful comparison:
    snapshot model (last frame only)
        versus
    temporal model (entire 120-second sequence)
"""

from __future__ import annotations

import argparse
import json
import random
from pathlib import Path

import joblib
import numpy as np
import torch
import torch.nn.functional as F
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

from ai_common import LABEL_ORDER, sha256_file


LABEL_TO_ID = {
    label: index
    for index, label in enumerate(LABEL_ORDER)
}

ID_TO_LABEL = {
    index: label
    for label, index in LABEL_TO_ID.items()
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


def decode_ids(values) -> np.ndarray:
    return np.asarray(
        [
            ID_TO_LABEL[int(value)]
            for value in values
        ],
        dtype=object,
    )


class CausalConv1d(nn.Module):
    def __init__(
        self,
        in_channels: int,
        out_channels: int,
        kernel_size: int,
        dilation: int,
    ) -> None:
        super().__init__()

        self.left_padding = (
            (kernel_size - 1) *
            dilation
        )

        self.conv = nn.Conv1d(
            in_channels,
            out_channels,
            kernel_size=kernel_size,
            dilation=dilation,
            padding=0,
        )

    def forward(self, x):
        x = F.pad(
            x,
            (
                self.left_padding,
                0,
            ),
        )

        return self.conv(x)


class TemporalResidualBlock(nn.Module):
    def __init__(
        self,
        channels: int,
        dilation: int,
        dropout: float,
    ) -> None:
        super().__init__()

        self.conv1 = CausalConv1d(
            channels,
            channels,
            kernel_size=3,
            dilation=dilation,
        )

        self.norm1 = nn.BatchNorm1d(
            channels
        )

        self.conv2 = CausalConv1d(
            channels,
            channels,
            kernel_size=3,
            dilation=dilation,
        )

        self.norm2 = nn.BatchNorm1d(
            channels
        )

        self.dropout = nn.Dropout(
            dropout
        )

        self.activation = nn.ReLU()

    def forward(self, x):
        residual = x

        x = self.conv1(x)
        x = self.norm1(x)
        x = self.activation(x)
        x = self.dropout(x)

        x = self.conv2(x)
        x = self.norm2(x)
        x = self.activation(x)
        x = self.dropout(x)

        return self.activation(
            x + residual
        )


class FaultDomainTCN(nn.Module):
    """
    Receptive field:
        2 causal convs per block,
        kernel size 3,
        dilations 1, 2, 4

    Effective receptive field = 29 timesteps,
    which covers the complete 24-step V3 input window.
    """

    def __init__(
        self,
        feature_count: int,
        class_count: int,
        channels: int = 64,
    ) -> None:
        super().__init__()

        self.input_projection = nn.Conv1d(
            feature_count,
            channels,
            kernel_size=1,
        )

        self.blocks = nn.Sequential(
            TemporalResidualBlock(
                channels,
                dilation=1,
                dropout=0.15,
            ),
            TemporalResidualBlock(
                channels,
                dilation=2,
                dropout=0.15,
            ),
            TemporalResidualBlock(
                channels,
                dilation=4,
                dropout=0.15,
            ),
        )

        self.classifier = nn.Sequential(
            nn.Linear(
                channels,
                32,
            ),
            nn.ReLU(),
            nn.Dropout(0.15),
            nn.Linear(
                32,
                class_count,
            ),
        )

    def forward(self, x):
        # Dataset shape:
        # [batch, timestep, feature]
        #
        # Conv1d shape:
        # [batch, feature, timestep]
        x = x.transpose(
            1,
            2,
        )

        x = self.input_projection(
            x
        )

        x = self.blocks(
            x
        )

        # Final causal state has access to the complete history.
        x = x[:, :, -1]

        return self.classifier(
            x
        )


def fit_sequence_scaler(
    x_train: np.ndarray,
) -> StandardScaler:
    scaler = StandardScaler()

    scaler.fit(
        x_train.reshape(
            -1,
            x_train.shape[-1],
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


def evaluate_loss(
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


def select_best_epoch(
    x_fit: np.ndarray,
    y_fit: np.ndarray,
    x_validation: np.ndarray,
    y_validation: np.ndarray,
    feature_count: int,
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
        feature_count=feature_count,
        class_count=class_count,
    ).to(device)

    criterion = nn.CrossEntropyLoss()

    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=learning_rate,
        weight_decay=1e-4,
    )

    train_loader = make_loader(
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
    best_validation_loss = float(
        "inf"
    )

    stale_epochs = 0
    history = []

    for epoch in range(
        1,
        max_epochs + 1,
    ):
        train_loss = run_training_epoch(
            model,
            train_loader,
            criterion,
            optimizer,
            device,
        )

        validation_loss = evaluate_loss(
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
            best_validation_loss - 1e-4
        )

        if improved:
            best_validation_loss = validation_loss
            best_epoch = epoch
            stale_epochs = 0
        else:
            stale_epochs += 1

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

        if stale_epochs >= patience:
            print(
                f"Early stopping after epoch {epoch}; "
                f"best epoch={best_epoch}."
            )
            break

    if best_epoch <= 0:
        raise RuntimeError(
            "Could not select a valid TCN epoch."
        )

    return (
        best_epoch,
        best_validation_loss,
        history,
    )


def train_final_model(
    x_train: np.ndarray,
    y_train: np.ndarray,
    feature_count: int,
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
        feature_count=feature_count,
        class_count=class_count,
    ).to(device)

    criterion = nn.CrossEntropyLoss()

    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=learning_rate,
        weight_decay=1e-4,
    )

    train_loader = make_loader(
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
        train_loss = run_training_epoch(
            model,
            train_loader,
            criterion,
            optimizer,
            device,
        )

        history.append(
            {
                "epoch": epoch,
                "train_loss": train_loss,
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
                f"loss={train_loss:.4f}"
            )

    return (
        model,
        history,
    )


def predict_probabilities(
    model: nn.Module,
    x: np.ndarray,
    batch_size: int,
    device,
) -> np.ndarray:
    dummy_labels = np.zeros(
        len(x),
        dtype=np.int64,
    )

    loader = make_loader(
        x,
        dummy_labels,
        batch_size,
        False,
        seed=1,
    )

    batches = []

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

            batches.append(
                probabilities
                .cpu()
                .numpy()
            )

    return normalize_probabilities(
        np.vstack(
            batches
        )
    )


def calculate_metrics(
    y_true: np.ndarray,
    probabilities: np.ndarray,
) -> dict:
    predicted_ids = np.argmax(
        probabilities,
        axis=1,
    )

    y_true_text = decode_ids(
        y_true
    )

    predicted_text = decode_ids(
        predicted_ids
    )

    return {
        "accuracy": accuracy_score(
            y_true_text,
            predicted_text,
        ),
        "balanced_accuracy": balanced_accuracy_score(
            y_true_text,
            predicted_text,
        ),
        "macro_precision": precision_score(
            y_true_text,
            predicted_text,
            labels=LABEL_ORDER,
            average="macro",
            zero_division=0,
        ),
        "macro_recall": recall_score(
            y_true_text,
            predicted_text,
            labels=LABEL_ORDER,
            average="macro",
            zero_division=0,
        ),
        "macro_f1": f1_score(
            y_true_text,
            predicted_text,
            labels=LABEL_ORDER,
            average="macro",
            zero_division=0,
        ),
        "log_loss": log_loss(
            y_true,
            probabilities,
            labels=list(
                range(
                    len(LABEL_ORDER)
                )
            ),
        ),
        "confusion_matrix": confusion_matrix(
            y_true_text,
            predicted_text,
            labels=LABEL_ORDER,
        ).tolist(),
        "classification_report": classification_report(
            y_true_text,
            predicted_text,
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


def print_confusion_matrix(
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


def print_class_recall(
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


def build_snapshot_xgb(
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

        y = dataset[
            "y"
        ].astype(
            np.int64
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

        timestep_seconds = dataset[
            "timestep_seconds"
        ].astype(
            np.float32
        )

    if label_order != LABEL_ORDER:
        raise RuntimeError(
            "Dataset label order does not match ai_common.LABEL_ORDER."
        )

    if x.ndim != 3:
        raise RuntimeError(
            f"Expected 3D sequence tensor, got shape {x.shape}."
        )

    if (
        len(x) != len(y) or
        x.shape[2] != len(feature_names)
    ):
        raise RuntimeError(
            "Temporal dataset shape metadata is inconsistent."
        )

    if not np.isfinite(x).all():
        raise RuntimeError(
            "Temporal dataset contains NaN or infinite values."
        )

    indices = np.arange(
        len(y)
    )

    (
        outer_train_indices,
        test_indices,
    ) = train_test_split(
        indices,
        test_size=args.test_size,
        random_state=args.seed,
        stratify=y,
    )

    (
        fit_indices,
        validation_indices,
    ) = train_test_split(
        outer_train_indices,
        test_size=args.validation_size,
        random_state=args.seed,
        stratify=y[
            outer_train_indices
        ],
    )

    fit_scaler = fit_sequence_scaler(
        x[
            fit_indices
        ]
    )

    x_fit = transform_sequences(
        fit_scaler,
        x[
            fit_indices
        ],
    )

    x_validation = transform_sequences(
        fit_scaler,
        x[
            validation_indices
        ],
    )

    y_fit = y[
        fit_indices
    ]

    y_validation = y[
        validation_indices
    ]

    device = torch.device(
        "cuda"
        if torch.cuda.is_available()
        else "cpu"
    )

    print()
    print(
        "AUTONOMOUS BASE STATION - "
        "TEMPORAL TCN V3"
    )
    print("=" * 64)
    print(
        f"Sequences          : {len(x)}"
    )
    print(
        f"Timesteps          : {x.shape[1]}"
    )
    print(
        f"Feature columns    : {x.shape[2]}"
    )
    print(
        f"Outer train rows   : "
        f"{len(outer_train_indices)}"
    )
    print(
        f"Fit rows           : "
        f"{len(fit_indices)}"
    )
    print(
        f"Validation rows    : "
        f"{len(validation_indices)}"
    )
    print(
        f"Untouched test rows: "
        f"{len(test_indices)}"
    )
    print(
        f"Device             : {device}"
    )
    print(
        "Leakage guard      : PASS"
    )
    print(
        "Outer test leakage : NONE"
    )
    print(
        "TCN receptive field: FULL 24-step window"
    )

    print()
    print(
        "[ PHASE 1 - SELECT BEST EPOCH ]"
    )

    (
        best_epoch,
        best_validation_loss,
        selection_history,
    ) = select_best_epoch(
        x_fit=x_fit,
        y_fit=y_fit,
        x_validation=x_validation,
        y_validation=y_validation,
        feature_count=x.shape[2],
        class_count=len(LABEL_ORDER),
        batch_size=args.batch_size,
        learning_rate=args.learning_rate,
        max_epochs=args.epochs,
        patience=args.patience,
        seed=args.seed,
        device=device,
    )

    print()
    print(
        f"Selected epoch     : {best_epoch}"
    )
    print(
        f"Best validation loss: "
        f"{best_validation_loss:.4f}"
    )

    print()
    print(
        "[ PHASE 2 - RETRAIN ON FULL OUTER TRAIN ]"
    )

    final_scaler = fit_sequence_scaler(
        x[
            outer_train_indices
        ]
    )

    x_outer_train = transform_sequences(
        final_scaler,
        x[
            outer_train_indices
        ],
    )

    x_test = transform_sequences(
        final_scaler,
        x[
            test_indices
        ],
    )

    y_outer_train = y[
        outer_train_indices
    ]

    y_test = y[
        test_indices
    ]

    (
        final_model,
        final_training_history,
    ) = train_final_model(
        x_train=x_outer_train,
        y_train=y_outer_train,
        feature_count=x.shape[2],
        class_count=len(LABEL_ORDER),
        batch_size=args.batch_size,
        learning_rate=args.learning_rate,
        epochs=best_epoch,
        seed=args.seed + 1000,
        device=device,
    )

    tcn_probabilities = predict_probabilities(
        final_model,
        x_test,
        args.batch_size,
        device,
    )

    tcn_metrics = calculate_metrics(
        y_test,
        tcn_probabilities,
    )

    print()
    print(
        "[ SNAPSHOT BASELINE - FINAL FRAME XGBOOST ]"
    )

    snapshot_model = build_snapshot_xgb(
        args.seed
    )

    snapshot_model.fit(
        x[
            outer_train_indices,
            -1,
            :,
        ],
        y_outer_train,
        verbose=False,
    )

    snapshot_probabilities = normalize_probabilities(
        snapshot_model.predict_proba(
            x[
                test_indices,
                -1,
                :,
            ]
        )
    )

    snapshot_metrics = calculate_metrics(
        y_test,
        snapshot_probabilities,
    )

    print_metrics(
        "FINAL-FRAME XGBOOST",
        snapshot_metrics,
    )

    print_metrics(
        "TEMPORAL TCN",
        tcn_metrics,
    )

    print_confusion_matrix(
        "TEMPORAL TCN",
        tcn_metrics[
            "confusion_matrix"
        ],
    )

    print_class_recall(
        "TEMPORAL TCN",
        tcn_metrics[
            "confusion_matrix"
        ],
    )

    accuracy_delta = (
        tcn_metrics["accuracy"] -
        snapshot_metrics["accuracy"]
    )

    macro_f1_delta = (
        tcn_metrics["macro_f1"] -
        snapshot_metrics["macro_f1"]
    )

    print()
    print(
        "[ TEMPORAL VALUE CHECK ]"
    )
    print(
        f"TCN accuracy delta : "
        f"{accuracy_delta:+.4f}"
    )
    print(
        f"TCN macro-F1 delta : "
        f"{macro_f1_delta:+.4f}"
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
        "fault_domain_temporal_tcn_v3.pt"
    )

    scaler_path = (
        model_dir /
        "fault_domain_temporal_tcn_v3_scaler.joblib"
    )

    metadata_path = (
        model_dir /
        "fault_domain_temporal_tcn_v3_metadata.json"
    )

    metrics_path = (
        result_dir /
        "fault_domain_temporal_tcn_v3_metrics.json"
    )

    cpu_state = {
        key: value.detach()
        .cpu()
        for key, value
        in final_model.state_dict().items()
    }

    torch.save(
        {
            "state_dict": cpu_state,
            "feature_count": x.shape[2],
            "class_count": len(LABEL_ORDER),
            "architecture": "causal_tcn_64_dilations_1_2_4",
        },
        model_path,
    )

    joblib.dump(
        final_scaler,
        scaler_path,
    )

    dataset_hash = sha256_file(
        args.dataset
    )

    timestep_interval = (
        float(
            timestep_seconds[1] -
            timestep_seconds[0]
        )
        if len(timestep_seconds) > 1
        else 0.0
    )

    metadata = {
        "schema": "abs.ml.model.v1",
        "model_name": "fault_domain_temporal_tcn_v3",
        "model_type": "PyTorchTCN",
        "hybrid_ai_role": "temporal_fault_domain_classifier",
        "target": "fault_domain",
        "classes": LABEL_ORDER,
        "feature_columns": feature_names,
        "feature_count": len(feature_names),
        "timesteps": int(
            x.shape[1]
        ),
        "sample_interval_seconds": (
            timestep_interval
        ),
        "dataset_sha256": dataset_hash,
        "outer_test_size": args.test_size,
        "inner_validation_size": args.validation_size,
        "random_state": args.seed,
        "architecture": {
            "input_features": int(
                x.shape[2]
            ),
            "channels": 64,
            "kernel_size": 3,
            "dilations": [
                1,
                2,
                4,
            ],
            "convolutions_per_block": 2,
            "causal": True,
            "receptive_field_covers_full_input": True,
            "head": [
                64,
                32,
                4,
            ],
        },
        "optimizer": "AdamW",
        "learning_rate": args.learning_rate,
        "weight_decay": 0.0001,
        "batch_size": args.batch_size,
        "selected_epoch": best_epoch,
        "best_validation_loss": best_validation_loss,
        "final_retrain_rows": int(
            len(outer_train_indices)
        ),
        "test_set_used_for_training": False,
        "scaler": "StandardScaler_fit_on_training_timesteps_only",
        "device_used": str(
            device
        ),
        "comparison_baseline": (
            "XGBoost using only final timestep "
            "of each V3 sequence"
        ),
    }

    metrics = {
        "schema": "abs.ml.metrics.v1",
        "model_name": "fault_domain_temporal_tcn_v3",
        "dataset_sha256": dataset_hash,
        "dataset_sequences": int(
            len(x)
        ),
        "outer_training_rows": int(
            len(outer_train_indices)
        ),
        "fit_rows_for_epoch_selection": int(
            len(fit_indices)
        ),
        "validation_rows_for_epoch_selection": int(
            len(validation_indices)
        ),
        "test_rows": int(
            len(test_indices)
        ),
        "selected_epoch": best_epoch,
        "best_validation_loss": (
            best_validation_loss
        ),
        "selection_history": (
            selection_history
        ),
        "final_training_history": (
            final_training_history
        ),
        "snapshot_xgboost_final_frame": (
            snapshot_metrics
        ),
        "temporal_tcn": (
            tcn_metrics
        ),
        "temporal_value_check": {
            "accuracy_delta": (
                accuracy_delta
            ),
            "macro_f1_delta": (
                macro_f1_delta
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
        f"Model              : "
        f"{model_path.resolve()}"
    )
    print(
        f"Scaler             : "
        f"{scaler_path.resolve()}"
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
        "Next stage         : interpret whether temporal history "
        "adds value, then add anomaly detection / autoencoder."
    )


if __name__ == "__main__":
    main()