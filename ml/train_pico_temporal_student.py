#!/usr/bin/env python3
"""Train and export a compact temporal fault-domain student for RP2040/Pico.

Input:
    24 timesteps x 33 features = 792 temporal inputs

Model:
    StandardScaler over flattened temporal positions
    Dense(792 -> 24) + ReLU + Dense(24 -> 4)

The model is deliberately small enough for a Raspberry Pi Pico while still
using the complete 120-second temporal window. It predicts only fault domain:
NORMAL / LOCAL / UPSTREAM / MIXED.

Source rule labels such as fault_label, operating_mode and guardrail_status
are NOT model inputs.
"""

from __future__ import annotations

import json
import random
from pathlib import Path

import numpy as np
import torch
from sklearn.metrics import (
    accuracy_score,
    balanced_accuracy_score,
    classification_report,
    confusion_matrix,
    f1_score,
)
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from torch import nn
from torch.utils.data import DataLoader, TensorDataset

from ai_common import LABEL_ORDER, sha256_file


SEED = 42
HIDDEN = 24
BATCH_SIZE = 256
MAX_EPOCHS = 100
PATIENCE = 12
LEARNING_RATE = 0.001


def set_seed(seed: int) -> None:
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)

    try:
        torch.use_deterministic_algorithms(
            True,
            warn_only=True,
        )
    except Exception:
        pass


class PicoTemporalStudent(nn.Module):
    def __init__(
        self,
        input_size: int,
        hidden: int,
        classes: int,
    ) -> None:
        super().__init__()

        self.net = nn.Sequential(
            nn.Linear(
                input_size,
                hidden,
            ),
            nn.ReLU(),
            nn.Linear(
                hidden,
                classes,
            ),
        )

    def forward(
        self,
        x,
    ):
        return self.net(
            x
        )


def train_epoch(
    model,
    loader,
    optimizer,
    loss_fn,
):
    model.train()

    total = 0.0
    rows = 0

    for xb, yb in loader:
        optimizer.zero_grad(
            set_to_none=True
        )

        logits = model(
            xb
        )

        loss = loss_fn(
            logits,
            yb
        )

        loss.backward()
        optimizer.step()

        total += (
            float(
                loss.item()
            ) *
            len(yb)
        )

        rows += len(yb)

    return total / max(
        rows,
        1,
    )


@torch.no_grad()
def eval_loss(
    model,
    x,
    y,
    loss_fn,
):
    model.eval()

    return float(
        loss_fn(
            model(x),
            y,
        ).item()
    )


def train_fixed_epochs(
    x,
    y,
    input_size: int,
    epochs: int,
):
    set_seed(
        SEED
    )

    model = PicoTemporalStudent(
        input_size,
        HIDDEN,
        len(
            LABEL_ORDER
        ),
    )

    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=LEARNING_RATE,
        weight_decay=1e-4,
    )

    loss_fn = nn.CrossEntropyLoss()

    generator = torch.Generator()
    generator.manual_seed(
        SEED
    )

    loader = DataLoader(
        TensorDataset(
            torch.from_numpy(
                x
            ),
            torch.from_numpy(
                y
            ),
        ),
        batch_size=BATCH_SIZE,
        shuffle=True,
        generator=generator,
    )

    for _ in range(
        epochs
    ):
        train_epoch(
            model,
            loader,
            optimizer,
            loss_fn,
        )

    return model


def format_c_float(
    value: float,
) -> str:
    text = f"{float(value):.9g}"

    if (
        "." not in text and
        "e" not in text.lower()
    ):
        text += ".0"

    return text + "f"


def format_float_array(
    name: str,
    values: np.ndarray,
    per_line: int = 6,
) -> str:
    flat = np.asarray(
        values,
        dtype=np.float32,
    ).reshape(
        -1
    )

    lines = []

    for start in range(
        0,
        len(flat),
        per_line,
    ):
        chunk = flat[
            start:
            start + per_line
        ]

        lines.append(
            "    " +
            ", ".join(
                format_c_float(v)
                for v in chunk
            )
        )

    return (
        f"static const float {name}[{len(flat)}] = {{\n" +
        ",\n".join(
            lines
        ) +
        "\n};\n"
    )


def export_header(
    path: Path,
    feature_names: list[str],
    timesteps: int,
    scaler: StandardScaler,
    model: PicoTemporalStudent,
) -> None:
    first = model.net[0]
    second = model.net[2]

    w1 = (
        first.weight
        .detach()
        .cpu()
        .numpy()
        .astype(
            np.float32
        )
    )

    b1 = (
        first.bias
        .detach()
        .cpu()
        .numpy()
        .astype(
            np.float32
        )
    )

    w2 = (
        second.weight
        .detach()
        .cpu()
        .numpy()
        .astype(
            np.float32
        )
    )

    b2 = (
        second.bias
        .detach()
        .cpu()
        .numpy()
        .astype(
            np.float32
        )
    )

    mean = np.asarray(
        scaler.mean_,
        dtype=np.float32,
    )

    scale = np.asarray(
        scaler.scale_,
        dtype=np.float32,
    )

    scale[
        scale == 0.0
    ] = 1.0

    feature_comment = "\n".join(
        f"// {index:02d}: {name}"
        for index, name in enumerate(
            feature_names
        )
    )

    text = f"""#pragma once

// AUTO-GENERATED by ml/train_pico_temporal_student.py
//
// Compact embedded temporal student:
//   {timesteps} timesteps x {len(feature_names)} features
//   flattened input -> Dense({HIDDEN}) -> ReLU -> Dense(4)
//
// Feature order inside EACH timestep:
{feature_comment}

#include <stdint.h>

static constexpr uint16_t PICO_MODEL_TIMESTEPS = {timesteps};
static constexpr uint16_t PICO_MODEL_FEATURES = {len(feature_names)};
static constexpr uint16_t PICO_MODEL_INPUTS = {timesteps * len(feature_names)};
static constexpr uint16_t PICO_MODEL_HIDDEN = {HIDDEN};
static constexpr uint16_t PICO_MODEL_CLASSES = {len(LABEL_ORDER)};

static const char *const PICO_MODEL_LABELS[PICO_MODEL_CLASSES] = {{
    "NORMAL",
    "LOCAL",
    "UPSTREAM",
    "MIXED"
}};

"""

    text += format_float_array(
        "PICO_MODEL_MEAN",
        mean,
    )

    text += "\n"

    text += format_float_array(
        "PICO_MODEL_SCALE",
        scale,
    )

    text += "\n"

    text += format_float_array(
        "PICO_MODEL_W1",
        w1,
    )

    text += "\n"

    text += format_float_array(
        "PICO_MODEL_B1",
        b1,
    )

    text += "\n"

    text += format_float_array(
        "PICO_MODEL_W2",
        w2,
    )

    text += "\n"

    text += format_float_array(
        "PICO_MODEL_B2",
        b2,
    )

    path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    path.write_text(
        text,
        encoding="utf-8",
    )


def main() -> int:
    here = Path(
        __file__
    ).resolve().parent

    root = here.parent

    dataset_path = (
        here /
        "data" /
        "base_station_temporal_dataset_v3.npz"
    )

    metadata_path = (
        here /
        "models" /
        "pico_temporal_student_v1_metadata.json"
    )

    metrics_path = (
        here /
        "results" /
        "pico_temporal_student_v1_metrics.json"
    )

    header_path = (
        root /
        "pico" /
        "include" /
        "pico_temporal_student_v1.h"
    )

    if not dataset_path.exists():
        raise FileNotFoundError(
            f"Temporal dataset not found: {dataset_path}"
        )

    set_seed(
        SEED
    )

    with np.load(
        dataset_path,
        allow_pickle=False,
    ) as data:
        x = data[
            "x"
        ].astype(
            np.float32
        )

        y = data[
            "y"
        ].astype(
            np.int64
        )

        feature_names = [
            str(v)
            for v in data[
                "feature_names"
            ].tolist()
        ]

        label_order = [
            str(v)
            for v in data[
                "label_order"
            ].tolist()
        ]

    if label_order != LABEL_ORDER:
        raise RuntimeError(
            "Dataset label order mismatch."
        )

    if x.shape[1:] != (
        24,
        33,
    ):
        raise RuntimeError(
            f"Expected temporal shape (*,24,33), got {x.shape}."
        )

    rows = len(
        y
    )

    indices = np.arange(
        rows
    )

    (
        outer_train,
        test_idx,
    ) = train_test_split(
        indices,
        test_size=0.20,
        random_state=SEED,
        stratify=y,
    )

    (
        fit_idx,
        val_idx,
    ) = train_test_split(
        outer_train,
        test_size=0.15,
        random_state=SEED,
        stratify=y[
            outer_train
        ],
    )

    flat = x.reshape(
        rows,
        -1,
    )

    inner_scaler = StandardScaler()

    x_fit = inner_scaler.fit_transform(
        flat[
            fit_idx
        ]
    ).astype(
        np.float32
    )

    x_val = inner_scaler.transform(
        flat[
            val_idx
        ]
    ).astype(
        np.float32
    )

    y_fit = y[
        fit_idx
    ]

    y_val = y[
        val_idx
    ]

    input_size = x_fit.shape[
        1
    ]

    model = PicoTemporalStudent(
        input_size,
        HIDDEN,
        len(
            LABEL_ORDER
        ),
    )

    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=LEARNING_RATE,
        weight_decay=1e-4,
    )

    loss_fn = nn.CrossEntropyLoss()

    generator = torch.Generator()
    generator.manual_seed(
        SEED
    )

    loader = DataLoader(
        TensorDataset(
            torch.from_numpy(
                x_fit
            ),
            torch.from_numpy(
                y_fit
            ),
        ),
        batch_size=BATCH_SIZE,
        shuffle=True,
        generator=generator,
    )

    x_val_t = torch.from_numpy(
        x_val
    )

    y_val_t = torch.from_numpy(
        y_val
    )

    best_epoch = 1
    best_loss = float(
        "inf"
    )

    stale = 0

    print()
    print(
        "AUTONOMOUS BASE STATION - PICO TEMPORAL STUDENT V1"
    )
    print("=" * 68)
    print(
        f"Sequences            : {rows}"
    )
    print(
        f"Temporal input       : 24 x 33 = {input_size}"
    )
    print(
        f"Hidden units         : {HIDDEN}"
    )
    print(
        f"Outer train          : {len(outer_train)}"
    )
    print(
        f"Untouched test       : {len(test_idx)}"
    )
    print()

    for epoch in range(
        1,
        MAX_EPOCHS + 1,
    ):
        train_value = train_epoch(
            model,
            loader,
            optimizer,
            loss_fn,
        )

        val_value = eval_loss(
            model,
            x_val_t,
            y_val_t,
            loss_fn,
        )

        if (
            epoch == 1 or
            epoch % 5 == 0
        ):
            print(
                f"Epoch {epoch:03d} | "
                f"train={train_value:.4f} | "
                f"val={val_value:.4f}"
            )

        if val_value < (
            best_loss -
            1e-5
        ):
            best_loss = val_value
            best_epoch = epoch
            stale = 0
        else:
            stale += 1

        if stale >= PATIENCE:
            break

    print()
    print(
        f"Selected epoch       : {best_epoch}"
    )
    print(
        f"Best validation loss : {best_loss:.6f}"
    )

    final_scaler = StandardScaler()

    x_train_final = final_scaler.fit_transform(
        flat[
            outer_train
        ]
    ).astype(
        np.float32
    )

    x_test = final_scaler.transform(
        flat[
            test_idx
        ]
    ).astype(
        np.float32
    )

    final_model = train_fixed_epochs(
        x_train_final,
        y[
            outer_train
        ],
        input_size,
        best_epoch,
    )

    final_model.eval()

    with torch.no_grad():
        logits = final_model(
            torch.from_numpy(
                x_test
            )
        )

        probabilities = torch.softmax(
            logits,
            dim=1,
        ).numpy()

    predictions = np.argmax(
        probabilities,
        axis=1,
    )

    y_test = y[
        test_idx
    ]

    accuracy = accuracy_score(
        y_test,
        predictions,
    )

    balanced = balanced_accuracy_score(
        y_test,
        predictions,
    )

    macro_f1 = f1_score(
        y_test,
        predictions,
        average="macro",
    )

    matrix = confusion_matrix(
        y_test,
        predictions,
        labels=np.arange(
            len(
                LABEL_ORDER
            )
        ),
    )

    report = classification_report(
        y_test,
        predictions,
        target_names=LABEL_ORDER,
        output_dict=True,
        zero_division=0,
    )

    print()
    print(
        "[ UNTOUCHED TEST ]"
    )
    print(
        f"Accuracy             : {accuracy:.4f}"
    )
    print(
        f"Balanced accuracy    : {balanced:.4f}"
    )
    print(
        f"Macro F1             : {macro_f1:.4f}"
    )
    print()
    print(
        "Confusion matrix:"
    )
    print(
        matrix
    )

    export_header(
        header_path,
        feature_names,
        x.shape[1],
        final_scaler,
        final_model,
    )

    parameter_count = sum(
        int(
            parameter.numel()
        )
        for parameter in final_model.parameters()
    )

    approx_model_bytes = (
        (
            parameter_count +
            2 * input_size
        ) *
        4
    )

    metadata = {
        "schema": "abs.ml.pico.student.v1",
        "model_name": "pico_temporal_student_v1",
        "model_type": "dense_temporal_student",
        "target": "fault_domain",
        "classes": LABEL_ORDER,
        "feature_columns": feature_names,
        "timesteps": int(
            x.shape[1]
        ),
        "feature_count": int(
            x.shape[2]
        ),
        "flattened_inputs": int(
            input_size
        ),
        "hidden_units": HIDDEN,
        "parameter_count": parameter_count,
        "approx_float_storage_bytes_including_scaler": (
            approx_model_bytes
        ),
        "training_dataset_sha256": sha256_file(
            dataset_path
        ),
        "outer_test_size": 0.20,
        "inner_validation_size": 0.15,
        "random_state": SEED,
        "selected_epoch": best_epoch,
        "best_validation_loss": best_loss,
        "embedded_header": str(
            header_path.relative_to(
                root
            )
        ).replace(
            "\\",
            "/",
        ),
        "notes": [
            "Uses full 24x33 temporal window.",
            "Rule/control reference labels are not model inputs.",
            "Compact student is separate from the full laptop TCN.",
        ],
    }

    metrics = {
        "schema": "abs.ml.pico.student.metrics.v1",
        "model_name": "pico_temporal_student_v1",
        "test_rows": int(
            len(
                y_test
            )
        ),
        "accuracy": float(
            accuracy
        ),
        "balanced_accuracy": float(
            balanced
        ),
        "macro_f1": float(
            macro_f1
        ),
        "confusion_matrix": (
            matrix.tolist()
        ),
        "classification_report": report,
    }

    metadata_path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    metrics_path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

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
        f"Embedded header      : {header_path}"
    )
    print(
        f"Parameter count      : {parameter_count}"
    )
    print(
        f"Approx float storage : {approx_model_bytes / 1024.0:.1f} KiB"
    )
    print()
    print(
        "PICO MODEL EXPORT: PASS"
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(
        main()
    )