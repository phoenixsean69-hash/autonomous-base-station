#!/usr/bin/env python3
"""
Deep MLP baseline for the Autonomous Base Station hybrid AI stage.

Fair comparison rules:
- SAME V2 dataset
- SAME 33 telemetry features
- SAME leakage guard
- SAME outer stratified 80/20 train/test split
- SAME random seed as RF and XGBoost
- test set remains untouched during training

The 80% training partition is split again into fit/validation subsets only
for early stopping. Feature normalization is fitted ONLY on the fit subset.

Architecture:
    33 -> 128 -> 64 -> 32 -> 4
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


def encode_labels(values) -> np.ndarray:
    return np.asarray(
        [
            LABEL_TO_ID[value]
            for value in values
        ],
        dtype=np.int64,
    )


def decode_labels(values) -> np.ndarray:
    return np.asarray(
        [
            ID_TO_LABEL[int(value)]
            for value in values
        ],
        dtype=object,
    )


class FaultDomainMLP(nn.Module):
    def __init__(
        self,
        input_dim: int,
        output_dim: int,
    ) -> None:
        super().__init__()

        self.network = nn.Sequential(
            nn.Linear(input_dim, 128),
            nn.ReLU(),
            nn.BatchNorm1d(128),
            nn.Dropout(0.20),

            nn.Linear(128, 64),
            nn.ReLU(),
            nn.BatchNorm1d(64),
            nn.Dropout(0.20),

            nn.Linear(64, 32),
            nn.ReLU(),
            nn.Dropout(0.15),

            nn.Linear(32, output_dim),
        )

    def forward(self, x):
        return self.network(x)


def to_tensor_dataset(
    x: np.ndarray,
    y: np.ndarray,
) -> TensorDataset:
    return TensorDataset(
        torch.tensor(
            x,
            dtype=torch.float32,
        ),
        torch.tensor(
            y,
            dtype=torch.long,
        ),
    )


def evaluate_loss(
    model: nn.Module,
    loader: DataLoader,
    criterion: nn.Module,
    device: torch.device,
) -> float:
    model.eval()

    total_loss = 0.0
    total_items = 0

    with torch.no_grad():
        for features, labels in loader:
            features = features.to(device)
            labels = labels.to(device)

            logits = model(features)
            loss = criterion(
                logits,
                labels,
            )

            batch_size = labels.size(0)
            total_loss += (
                float(loss.item()) *
                batch_size
            )

            total_items += batch_size

    if total_items == 0:
        return 0.0

    return total_loss / total_items


def predict_probabilities(
    model: nn.Module,
    x: np.ndarray,
    device: torch.device,
    batch_size: int,
) -> np.ndarray:
    model.eval()

    loader = DataLoader(
        TensorDataset(
            torch.tensor(
                x,
                dtype=torch.float32,
            )
        ),
        batch_size=batch_size,
        shuffle=False,
    )

    chunks = []

    with torch.no_grad():
        for (features,) in loader:
            features = features.to(device)

            logits = model(features)

            probabilities = torch.softmax(
                logits,
                dim=1,
            )

            chunks.append(
                probabilities.cpu().numpy()
            )

    return np.concatenate(
        chunks,
        axis=0,
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
        default=180,
    )

    parser.add_argument(
        "--patience",
        type=int,
        default=18,
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

    set_deterministic(
        args.seed
    )

    df = load_dataset(
        args.dataset
    )

    feature_columns = build_feature_list(
        df
    )

    (
        x_train_outer,
        x_test,
        y_train_outer_text,
        y_test_text,
    ) = split_fault_domain_dataset(
        df,
        feature_columns,
        args.test_size,
        args.seed,
    )

    (
        x_fit,
        x_val,
        y_fit_text,
        y_val_text,
    ) = train_test_split(
        x_train_outer,
        y_train_outer_text,
        test_size=args.validation_size,
        random_state=args.seed,
        stratify=y_train_outer_text,
    )

    scaler = StandardScaler()

    x_fit_scaled = scaler.fit_transform(
        x_fit
    ).astype(np.float32)

    x_val_scaled = scaler.transform(
        x_val
    ).astype(np.float32)

    x_test_scaled = scaler.transform(
        x_test
    ).astype(np.float32)

    y_fit = encode_labels(
        y_fit_text
    )

    y_val = encode_labels(
        y_val_text
    )

    y_test = encode_labels(
        y_test_text
    )

    train_dataset = to_tensor_dataset(
        x_fit_scaled,
        y_fit,
    )

    val_dataset = to_tensor_dataset(
        x_val_scaled,
        y_val,
    )

    generator = torch.Generator()
    generator.manual_seed(
        args.seed
    )

    train_loader = DataLoader(
        train_dataset,
        batch_size=args.batch_size,
        shuffle=True,
        generator=generator,
    )

    val_loader = DataLoader(
        val_dataset,
        batch_size=args.batch_size,
        shuffle=False,
    )

    device = torch.device(
        "cuda"
        if torch.cuda.is_available()
        else "cpu"
    )

    model = FaultDomainMLP(
        input_dim=len(
            feature_columns
        ),
        output_dim=len(
            LABEL_ORDER
        ),
    ).to(device)

    criterion = nn.CrossEntropyLoss()

    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=args.learning_rate,
        weight_decay=1e-4,
    )

    print()
    print(
        "AUTONOMOUS BASE STATION - "
        "HYBRID AI STAGE / DEEP MLP"
    )
    print("=" * 64)
    print(f"Dataset rows      : {len(df)}")
    print(f"Feature columns   : {len(feature_columns)}")
    print(f"Outer train rows  : {len(x_train_outer)}")
    print(f"Fit rows          : {len(x_fit)}")
    print(f"Validation rows   : {len(x_val)}")
    print(f"Test rows         : {len(x_test)}")
    print(f"Device            : {device}")
    print("Leakage guard     : PASS")
    print("Test split        : SAME AS RF/XGBOOST")
    print("Scaler fit        : TRAINING DATA ONLY")
    print()
    print(
        "Architecture      : "
        "33 -> 128 -> 64 -> 32 -> 4"
    )
    print(
        f"Max epochs        : {args.epochs}"
    )
    print(
        f"Early-stop patience: {args.patience}"
    )
    print()

    best_val_loss = float("inf")
    best_state = None
    best_epoch = 0
    epochs_without_improvement = 0

    history = []

    for epoch in range(
        1,
        args.epochs + 1,
    ):
        model.train()

        total_train_loss = 0.0
        total_train_items = 0

        for features, labels in train_loader:
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

            optimizer.step()

            batch_size = labels.size(0)

            total_train_loss += (
                float(loss.item()) *
                batch_size
            )

            total_train_items += (
                batch_size
            )

        train_loss = (
            total_train_loss /
            total_train_items
        )

        val_loss = evaluate_loss(
            model,
            val_loader,
            criterion,
            device,
        )

        history.append(
            {
                "epoch": epoch,
                "train_loss": train_loss,
                "validation_loss": val_loss,
            }
        )

        improved = (
            val_loss <
            best_val_loss - 1e-5
        )

        if improved:
            best_val_loss = val_loss
            best_epoch = epoch

            best_state = {
                key: value.detach()
                .cpu()
                .clone()
                for key, value
                in model.state_dict().items()
            }

            epochs_without_improvement = 0

        else:
            epochs_without_improvement += 1

        if (
            epoch == 1 or
            epoch % 10 == 0 or
            improved and epoch <= 5
        ):
            print(
                f"Epoch {epoch:>3} | "
                f"train={train_loss:.4f} | "
                f"val={val_loss:.4f}"
            )

        if (
            epochs_without_improvement
            >= args.patience
        ):
            print()
            print(
                f"Early stopping at epoch "
                f"{epoch}; best epoch "
                f"{best_epoch}."
            )

            break

    if best_state is None:
        raise RuntimeError(
            "Training did not produce "
            "a valid checkpoint."
        )

    model.load_state_dict(
        best_state
    )

    model.to(device)

    probabilities = predict_probabilities(
        model,
        x_test_scaled,
        device,
        args.batch_size,
    )

    predicted_ids = np.argmax(
        probabilities,
        axis=1,
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
        "fault_domain_deep_mlp_v2.pt"
    )

    scaler_path = (
        model_dir /
        "fault_domain_deep_mlp_v2_scaler.joblib"
    )

    metadata_path = (
        model_dir /
        "fault_domain_deep_mlp_v2_metadata.json"
    )

    metrics_path = (
        result_dir /
        "fault_domain_deep_mlp_v2_metrics.json"
    )

    checkpoint = {
        "state_dict": best_state,
        "input_dim": len(
            feature_columns
        ),
        "output_dim": len(
            LABEL_ORDER
        ),
        "classes": LABEL_ORDER,
        "architecture": [
            128,
            64,
            32,
        ],
    }

    torch.save(
        checkpoint,
        model_path,
    )

    joblib.dump(
        scaler,
        scaler_path,
    )

    dataset_hash = sha256_file(
        args.dataset
    )

    metadata = {
        "schema": "abs.ml.model.v1",
        "model_name": "fault_domain_deep_mlp_v2",
        "model_type": "PyTorchMLP",
        "hybrid_ai_role": "deep_snapshot_classifier",
        "target": "fault_domain",
        "classes": LABEL_ORDER,
        "label_to_id": LABEL_TO_ID,
        "feature_columns": feature_columns,
        "feature_count": len(
            feature_columns
        ),
        "dataset_sha256": dataset_hash,
        "outer_test_size": args.test_size,
        "inner_validation_size": (
            args.validation_size
        ),
        "random_state": args.seed,
        "architecture": [
            len(feature_columns),
            128,
            64,
            32,
            len(LABEL_ORDER),
        ],
        "activation": "ReLU",
        "optimizer": "AdamW",
        "learning_rate": (
            args.learning_rate
        ),
        "best_epoch": best_epoch,
        "best_validation_loss": (
            best_val_loss
        ),
        "device_used": str(
            device
        ),
        "scaler": "StandardScaler_fit_on_training_only",
    }

    metrics = {
        "schema": "abs.ml.metrics.v1",
        "model_name": "fault_domain_deep_mlp_v2",
        "dataset_sha256": dataset_hash,
        "dataset_rows": len(df),
        "outer_training_rows": len(
            x_train_outer
        ),
        "fit_rows": len(
            x_fit
        ),
        "validation_rows": len(
            x_val
        ),
        "test_rows": len(
            x_test
        ),
        "accuracy": accuracy,
        "balanced_accuracy": balanced_accuracy,
        "macro_precision": macro_precision,
        "macro_recall": macro_recall,
        "macro_f1": macro_f1,
        "log_loss": multiclass_log_loss,
        "label_order": LABEL_ORDER,
        "confusion_matrix": matrix.tolist(),
        "classification_report": report,
        "best_epoch": best_epoch,
        "best_validation_loss": best_val_loss,
        "training_history": history,
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
    print(
        f"Accuracy          : "
        f"{accuracy:.4f}"
    )
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
    print(
        f"Macro F1          : "
        f"{macro_f1:.4f}"
    )
    print(
        f"Log Loss          : "
        f"{multiclass_log_loss:.4f}"
    )
    print(
        f"Best Epoch        : "
        f"{best_epoch}"
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
    print(
        f"Model             : "
        f"{model_path.resolve()}"
    )
    print(
        f"Scaler            : "
        f"{scaler_path.resolve()}"
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
        "Next hybrid stage : compare "
        "RF vs XGBoost vs Deep MLP, "
        "then build the ensemble."
    )


if __name__ == "__main__":
    main()