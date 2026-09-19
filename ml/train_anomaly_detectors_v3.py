#!/usr/bin/env python3
"""
Unsupervised anomaly detection for Autonomous Base Station temporal V3 data.

Two detectors are evaluated on the exact same untouched outer test split:

1. Isolation Forest baseline
2. Temporal convolutional autoencoder

Critical rule:
    BOTH detectors are trained only on NORMAL sequences.

Fault labels are never used to fit the anomaly detectors or set thresholds.
The threshold is calibrated only from held-out NORMAL calibration sequences.

Known LOCAL / UPSTREAM / MIXED sequences are used only for evaluation.
This tests whether the system can flag behaviour that departs from learned
normal operation before a supervised fault-domain classifier is consulted.
"""

from __future__ import annotations

import argparse
import json
import random
from pathlib import Path

import joblib
import numpy as np
import torch
from sklearn.ensemble import IsolationForest
from sklearn.metrics import (
    average_precision_score,
    confusion_matrix,
    roc_auc_score,
)
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from torch import nn
from torch.utils.data import DataLoader, TensorDataset

from ai_common import LABEL_ORDER, sha256_file


NORMAL_ID = LABEL_ORDER.index(
    "NORMAL"
)


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


class TemporalAutoencoder(nn.Module):
    """
    Input:
        [batch, timestep=24, feature=33]

    Encoder:
        33 -> 64
        24 -> 12
        12 -> 6
        channels 64 -> 32 -> 16

    Decoder reconstructs the original 24 x 33 sequence.
    """

    def __init__(
        self,
        feature_count: int,
    ) -> None:
        super().__init__()

        self.encoder = nn.Sequential(
            nn.Conv1d(
                feature_count,
                64,
                kernel_size=3,
                padding=1,
            ),
            nn.ReLU(),

            nn.Conv1d(
                64,
                32,
                kernel_size=4,
                stride=2,
                padding=1,
            ),
            nn.ReLU(),

            nn.Conv1d(
                32,
                16,
                kernel_size=4,
                stride=2,
                padding=1,
            ),
            nn.ReLU(),
        )

        self.decoder = nn.Sequential(
            nn.ConvTranspose1d(
                16,
                32,
                kernel_size=4,
                stride=2,
                padding=1,
            ),
            nn.ReLU(),

            nn.ConvTranspose1d(
                32,
                64,
                kernel_size=4,
                stride=2,
                padding=1,
            ),
            nn.ReLU(),

            nn.Conv1d(
                64,
                feature_count,
                kernel_size=3,
                padding=1,
            ),
        )

    def forward(self, x):
        x = x.transpose(
            1,
            2,
        )

        latent = self.encoder(
            x
        )

        reconstructed = self.decoder(
            latent
        )

        return reconstructed.transpose(
            1,
            2,
        )


def fit_feature_scaler(
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
    batch_size: int,
    shuffle: bool,
    seed: int,
) -> DataLoader:
    dataset = TensorDataset(
        torch.tensor(
            x,
            dtype=torch.float32,
        )
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


def train_epoch(
    model: nn.Module,
    loader: DataLoader,
    optimizer,
    criterion,
    device,
) -> float:
    model.train()

    total_loss = 0.0
    total_items = 0

    for (features,) in loader:
        features = features.to(
            device
        )

        optimizer.zero_grad(
            set_to_none=True
        )

        reconstructed = model(
            features
        )

        loss = criterion(
            reconstructed,
            features,
        )

        loss.backward()

        torch.nn.utils.clip_grad_norm_(
            model.parameters(),
            max_norm=5.0,
        )

        optimizer.step()

        count = features.size(0)

        total_loss += (
            float(loss.item()) *
            count
        )

        total_items += count

    return (
        total_loss /
        total_items
    )


def validation_loss(
    model: nn.Module,
    loader: DataLoader,
    criterion,
    device,
) -> float:
    model.eval()

    total_loss = 0.0
    total_items = 0

    with torch.no_grad():
        for (features,) in loader:
            features = features.to(
                device
            )

            reconstructed = model(
                features
            )

            loss = criterion(
                reconstructed,
                features,
            )

            count = features.size(0)

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
    x_validation: np.ndarray,
    feature_count: int,
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

    model = TemporalAutoencoder(
        feature_count
    ).to(device)

    criterion = nn.MSELoss()

    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=learning_rate,
        weight_decay=1e-5,
    )

    fit_loader = make_loader(
        x_fit,
        batch_size,
        True,
        seed,
    )

    validation_loader = make_loader(
        x_validation,
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
        current_train_loss = train_epoch(
            model,
            fit_loader,
            optimizer,
            criterion,
            device,
        )

        current_validation_loss = validation_loss(
            model,
            validation_loader,
            criterion,
            device,
        )

        history.append(
            {
                "epoch": epoch,
                "train_loss": current_train_loss,
                "validation_loss": current_validation_loss,
            }
        )

        improved = (
            current_validation_loss <
            best_loss - 1e-5
        )

        if improved:
            best_loss = (
                current_validation_loss
            )

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
                f"train={current_train_loss:.5f} | "
                f"val={current_validation_loss:.5f}"
            )

        if stale >= patience:
            print(
                f"Early stopping after epoch {epoch}; "
                f"best epoch={best_epoch}."
            )
            break

    if best_epoch <= 0:
        raise RuntimeError(
            "Could not select a valid autoencoder epoch."
        )

    return (
        best_epoch,
        best_loss,
        history,
    )


def train_final_autoencoder(
    x_train: np.ndarray,
    feature_count: int,
    batch_size: int,
    learning_rate: float,
    epochs: int,
    seed: int,
    device,
):
    set_deterministic(
        seed
    )

    model = TemporalAutoencoder(
        feature_count
    ).to(device)

    criterion = nn.MSELoss()

    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=learning_rate,
        weight_decay=1e-5,
    )

    loader = make_loader(
        x_train,
        batch_size,
        True,
        seed,
    )

    history = []

    for epoch in range(
        1,
        epochs + 1,
    ):
        loss = train_epoch(
            model,
            loader,
            optimizer,
            criterion,
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
                f"loss={loss:.5f}"
            )

    return (
        model,
        history,
    )


def reconstruction_scores(
    model: nn.Module,
    x: np.ndarray,
    batch_size: int,
    device,
) -> np.ndarray:
    loader = make_loader(
        x,
        batch_size,
        False,
        seed=1,
    )

    scores = []

    model.eval()

    with torch.no_grad():
        for (features,) in loader:
            features = features.to(
                device
            )

            reconstructed = model(
                features
            )

            sample_error = torch.mean(
                (
                    reconstructed -
                    features
                ) ** 2,
                dim=(
                    1,
                    2,
                ),
            )

            scores.append(
                sample_error
                .cpu()
                .numpy()
            )

    return np.concatenate(
        scores
    ).astype(
        np.float64
    )


def anomaly_metrics(
    y_domain: np.ndarray,
    scores: np.ndarray,
    threshold: float,
) -> dict:
    y_anomaly = (
        y_domain != NORMAL_ID
    ).astype(
        np.int64
    )

    predicted = (
        scores > threshold
    ).astype(
        np.int64
    )

    cm = confusion_matrix(
        y_anomaly,
        predicted,
        labels=[
            0,
            1,
        ],
    )

    tn, fp, fn, tp = (
        int(cm[0, 0]),
        int(cm[0, 1]),
        int(cm[1, 0]),
        int(cm[1, 1]),
    )

    normal_mask = (
        y_domain == NORMAL_ID
    )

    fault_mask = ~normal_mask

    per_domain = {}

    for class_id, label in enumerate(
        LABEL_ORDER
    ):
        mask = (
            y_domain == class_id
        )

        if not np.any(mask):
            continue

        rate = float(
            predicted[mask].mean()
        )

        per_domain[label] = {
            (
                "false_positive_rate"
                if label == "NORMAL"
                else "detection_rate"
            ): rate,
            "mean_score": float(
                np.mean(
                    scores[mask]
                )
            ),
            "median_score": float(
                np.median(
                    scores[mask]
                )
            ),
            "count": int(
                mask.sum()
            ),
        }

    return {
        "threshold": float(
            threshold
        ),
        "roc_auc": float(
            roc_auc_score(
                y_anomaly,
                scores,
            )
        ),
        "average_precision": float(
            average_precision_score(
                y_anomaly,
                scores,
            )
        ),
        "normal_false_positive_rate": float(
            predicted[
                normal_mask
            ].mean()
        ),
        "known_fault_detection_rate": float(
            predicted[
                fault_mask
            ].mean()
        ),
        "binary_confusion_matrix": [
            [
                tn,
                fp,
            ],
            [
                fn,
                tp,
            ],
        ],
        "per_domain": per_domain,
    }


def print_detector(
    name: str,
    metrics: dict,
) -> None:
    print()
    print(
        f"[ {name} ]"
    )
    print(
        f"Threshold          : "
        f"{metrics['threshold']:.6f}"
    )
    print(
        f"ROC AUC            : "
        f"{metrics['roc_auc']:.4f}"
    )
    print(
        f"Average Precision  : "
        f"{metrics['average_precision']:.4f}"
    )
    print(
        f"NORMAL false alarm : "
        f"{metrics['normal_false_positive_rate']:.4f}"
    )
    print(
        f"Known fault detect : "
        f"{metrics['known_fault_detection_rate']:.4f}"
    )

    print()
    print(
        f"[ {name} - DOMAIN DETECTION ]"
    )

    for label in LABEL_ORDER:
        row = metrics[
            "per_domain"
        ][label]

        if label == "NORMAL":
            value = row[
                "false_positive_rate"
            ]

            print(
                f"{label:<12}: "
                f"false alarm {value:.4f} "
                f"| median score {row['median_score']:.6f}"
            )

        else:
            value = row[
                "detection_rate"
            ]

            print(
                f"{label:<12}: "
                f"detected {value:.4f} "
                f"| median score {row['median_score']:.6f}"
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
        "--calibration-size",
        type=float,
        default=0.15,
    )

    parser.add_argument(
        "--validation-size",
        type=float,
        default=0.15,
    )

    parser.add_argument(
        "--threshold-percentile",
        type=float,
        default=99.0,
    )

    parser.add_argument(
        "--seed",
        type=int,
        default=42,
    )

    parser.add_argument(
        "--epochs",
        type=int,
        default=100,
    )

    parser.add_argument(
        "--patience",
        type=int,
        default=12,
    )

    parser.add_argument(
        "--batch-size",
        type=int,
        default=128,
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

    if label_order != LABEL_ORDER:
        raise RuntimeError(
            "V3 label order does not match ai_common.LABEL_ORDER."
        )

    if not np.isfinite(x).all():
        raise RuntimeError(
            "Dataset contains NaN or infinite values."
        )

    all_indices = np.arange(
        len(y)
    )

    (
        outer_train_indices,
        test_indices,
    ) = train_test_split(
        all_indices,
        test_size=args.test_size,
        random_state=args.seed,
        stratify=y,
    )

    normal_outer_train = (
        outer_train_indices[
            y[
                outer_train_indices
            ] == NORMAL_ID
        ]
    )

    (
        normal_model_pool,
        normal_calibration,
    ) = train_test_split(
        normal_outer_train,
        test_size=args.calibration_size,
        random_state=args.seed,
        shuffle=True,
    )

    (
        normal_fit,
        normal_validation,
    ) = train_test_split(
        normal_model_pool,
        test_size=args.validation_size,
        random_state=args.seed,
        shuffle=True,
    )

    selection_scaler = fit_feature_scaler(
        x[
            normal_fit
        ]
    )

    x_fit = transform_sequences(
        selection_scaler,
        x[
            normal_fit
        ],
    )

    x_validation = transform_sequences(
        selection_scaler,
        x[
            normal_validation
        ],
    )

    device = torch.device(
        "cuda"
        if torch.cuda.is_available()
        else "cpu"
    )

    print()
    print(
        "AUTONOMOUS BASE STATION - "
        "TEMPORAL ANOMALY DETECTION V3"
    )
    print("=" * 68)
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
        f"NORMAL outer train  : "
        f"{len(normal_outer_train)}"
    )
    print(
        f"NORMAL model pool   : "
        f"{len(normal_model_pool)}"
    )
    print(
        f"NORMAL fit          : "
        f"{len(normal_fit)}"
    )
    print(
        f"NORMAL validation   : "
        f"{len(normal_validation)}"
    )
    print(
        f"NORMAL calibration  : "
        f"{len(normal_calibration)}"
    )
    print(
        f"Device              : {device}"
    )
    print(
        "Training labels     : NORMAL ONLY"
    )
    print(
        "Fault labels in fit : NO"
    )
    print(
        "Outer test leakage  : NONE"
    )

    print()
    print(
        "[ PHASE 1 - AUTOENCODER EPOCH SELECTION ]"
    )

    (
        best_epoch,
        best_validation_loss,
        selection_history,
    ) = select_best_epoch(
        x_fit=x_fit,
        x_validation=x_validation,
        feature_count=x.shape[2],
        batch_size=args.batch_size,
        learning_rate=args.learning_rate,
        max_epochs=args.epochs,
        patience=args.patience,
        seed=args.seed,
        device=device,
    )

    print()
    print(
        f"Selected epoch      : {best_epoch}"
    )
    print(
        f"Best validation loss: "
        f"{best_validation_loss:.6f}"
    )

    final_scaler = fit_feature_scaler(
        x[
            normal_model_pool
        ]
    )

    x_model_pool = transform_sequences(
        final_scaler,
        x[
            normal_model_pool
        ],
    )

    x_calibration = transform_sequences(
        final_scaler,
        x[
            normal_calibration
        ],
    )

    x_test = transform_sequences(
        final_scaler,
        x[
            test_indices
        ],
    )

    print()
    print(
        "[ PHASE 2 - FINAL AUTOENCODER ]"
    )

    (
        autoencoder,
        final_training_history,
    ) = train_final_autoencoder(
        x_train=x_model_pool,
        feature_count=x.shape[2],
        batch_size=args.batch_size,
        learning_rate=args.learning_rate,
        epochs=best_epoch,
        seed=args.seed + 1000,
        device=device,
    )

    calibration_ae_scores = reconstruction_scores(
        autoencoder,
        x_calibration,
        args.batch_size,
        device,
    )

    ae_threshold = float(
        np.percentile(
            calibration_ae_scores,
            args.threshold_percentile,
        )
    )

    test_ae_scores = reconstruction_scores(
        autoencoder,
        x_test,
        args.batch_size,
        device,
    )

    ae_metrics = anomaly_metrics(
        y[
            test_indices
        ],
        test_ae_scores,
        ae_threshold,
    )

    print()
    print(
        "[ PHASE 3 - ISOLATION FOREST BASELINE ]"
    )

    isolation_forest = IsolationForest(
        n_estimators=300,
        max_samples="auto",
        contamination="auto",
        random_state=args.seed,
        n_jobs=-1,
    )

    isolation_forest.fit(
        x_model_pool.reshape(
            len(x_model_pool),
            -1,
        )
    )

    calibration_if_scores = -isolation_forest.score_samples(
        x_calibration.reshape(
            len(x_calibration),
            -1,
        )
    )

    if_threshold = float(
        np.percentile(
            calibration_if_scores,
            args.threshold_percentile,
        )
    )

    test_if_scores = -isolation_forest.score_samples(
        x_test.reshape(
            len(x_test),
            -1,
        )
    )

    if_metrics = anomaly_metrics(
        y[
            test_indices
        ],
        test_if_scores,
        if_threshold,
    )

    print_detector(
        "ISOLATION FOREST",
        if_metrics,
    )

    print_detector(
        "TEMPORAL AUTOENCODER",
        ae_metrics,
    )

    print()
    print(
        "[ ANOMALY MODEL COMPARISON ]"
    )
    print(
        f"ROC AUC delta (AE - IF)       : "
        f"{ae_metrics['roc_auc'] - if_metrics['roc_auc']:+.4f}"
    )
    print(
        f"Fault detection delta (AE-IF) : "
        f"{ae_metrics['known_fault_detection_rate'] - if_metrics['known_fault_detection_rate']:+.4f}"
    )
    print(
        f"False alarm delta (AE-IF)      : "
        f"{ae_metrics['normal_false_positive_rate'] - if_metrics['normal_false_positive_rate']:+.4f}"
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

    ae_model_path = (
        model_dir /
        "temporal_autoencoder_v3.pt"
    )

    scaler_path = (
        model_dir /
        "temporal_anomaly_v3_scaler.joblib"
    )

    if_model_path = (
        model_dir /
        "temporal_isolation_forest_v3.joblib"
    )

    metadata_path = (
        model_dir /
        "temporal_anomaly_v3_metadata.json"
    )

    metrics_path = (
        result_dir /
        "temporal_anomaly_v3_metrics.json"
    )

    torch.save(
        {
            "state_dict": {
                key: value.detach().cpu()
                for key, value
                in autoencoder.state_dict().items()
            },
            "feature_count": int(
                x.shape[2]
            ),
            "timesteps": int(
                x.shape[1]
            ),
            "architecture": (
                "temporal_conv_autoencoder_64_32_16"
            ),
        },
        ae_model_path,
    )

    joblib.dump(
        final_scaler,
        scaler_path,
    )

    joblib.dump(
        isolation_forest,
        if_model_path,
    )

    dataset_hash = sha256_file(
        args.dataset
    )

    metadata = {
        "schema": "abs.ml.anomaly.v1",
        "model_name": "temporal_anomaly_v3",
        "dataset_sha256": dataset_hash,
        "feature_columns": feature_names,
        "feature_count": len(
            feature_names
        ),
        "timesteps": int(
            x.shape[1]
        ),
        "classes_used_for_training": [
            "NORMAL",
        ],
        "fault_labels_used_for_training": False,
        "outer_test_used_for_training": False,
        "random_state": args.seed,
        "threshold_percentile": (
            args.threshold_percentile
        ),
        "threshold_calibration": (
            "held_out_NORMAL_outer_training_sequences"
        ),
        "autoencoder": {
            "type": (
                "PyTorchTemporalConvolutionalAutoencoder"
            ),
            "selected_epoch": best_epoch,
            "best_validation_loss": (
                best_validation_loss
            ),
            "learning_rate": (
                args.learning_rate
            ),
            "device_used": str(
                device
            ),
        },
        "isolation_forest": {
            "type": "IsolationForest",
            "n_estimators": 300,
        },
    }

    metrics = {
        "schema": "abs.ml.metrics.v1",
        "model_name": "temporal_anomaly_v3",
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
        "normal_outer_training_rows": int(
            len(normal_outer_train)
        ),
        "normal_model_pool_rows": int(
            len(normal_model_pool)
        ),
        "normal_fit_rows": int(
            len(normal_fit)
        ),
        "normal_validation_rows": int(
            len(normal_validation)
        ),
        "normal_calibration_rows": int(
            len(normal_calibration)
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
        "isolation_forest": (
            if_metrics
        ),
        "temporal_autoencoder": (
            ae_metrics
        ),
        "comparison": {
            "roc_auc_delta_autoencoder_minus_isolation_forest": (
                ae_metrics["roc_auc"] -
                if_metrics["roc_auc"]
            ),
            "fault_detection_delta_autoencoder_minus_isolation_forest": (
                ae_metrics[
                    "known_fault_detection_rate"
                ] -
                if_metrics[
                    "known_fault_detection_rate"
                ]
            ),
            "false_alarm_delta_autoencoder_minus_isolation_forest": (
                ae_metrics[
                    "normal_false_positive_rate"
                ] -
                if_metrics[
                    "normal_false_positive_rate"
                ]
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
        f"Autoencoder        : "
        f"{ae_model_path.resolve()}"
    )
    print(
        f"Isolation Forest   : "
        f"{if_model_path.resolve()}"
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
        "Next stage         : integrate anomaly score with "
        "TCN/XGBoost diagnosis and then build hierarchical root-cause models."
    )


if __name__ == "__main__":
    main()