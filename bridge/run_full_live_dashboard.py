#!/usr/bin/env python3
"""
Start the full live ABS simulation data path for the dashboard.

Requires:
  - ESP32 Wokwi running on RFC2217 port 4001
  - Pico Wokwi running on RFC2217 port 4000
  - local trained AI artifacts under ml/models

This launcher clears only the previous runtime AI snapshot before starting,
then delegates to the existing bridge/live_ai_bridge.py. It does not change
firmware, model files, or dashboard code.
"""

from __future__ import annotations

import os
import socket
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

ESP32_HOST = "127.0.0.1"
ESP32_PORT = 4001
PICO_HOST = "127.0.0.1"
PICO_PORT = 4000

REQUIRED_MODELS = [
    "fault_domain_temporal_tcn_v3.pt",
    "fault_domain_temporal_tcn_v3_scaler.joblib",
    "local_root_cause_tcn_v3.pt",
    "local_root_cause_tcn_v3_scaler.joblib",
    "upstream_root_cause_tcn_v3.pt",
    "upstream_root_cause_tcn_v3_scaler.joblib",
    "temporal_autoencoder_v3.pt",
    "temporal_anomaly_v3_scaler.joblib",
    "fault_domain_temporal_tcn_v3_metadata.json",
    "hierarchical_root_cause_v3_metadata.json",
    "temporal_anomaly_v3_metadata.json",
]

REQUIRED_RESULTS = [
    "fault_domain_temporal_tcn_v3_metrics.json",
    "hierarchical_root_cause_v3_metrics.json",
    "temporal_anomaly_v3_metrics.json",
    "unified_ai_v3_metrics.json",
]


def port_open(host: str, port: int) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.75):
            return True
    except OSError:
        return False


def fail(message: str, code: int = 1) -> int:
    print(f"[FAIL] {message}")
    return code


def main() -> int:
    print()
    print("============================================================")
    print(" ABS FULL LIVE SIMULATION -> DASHBOARD")
    print("============================================================")
    print()
    print("Expected chain:")
    print("  Wokwi ESP32 :4001")
    print("      -> Laptop temporal AI")
    print("      -> Wokwi Pico :4000")
    print("      -> ESP32 deterministic guardrails")
    print("      -> dashboard runtime snapshots")
    print()

    if not port_open(ESP32_HOST, ESP32_PORT):
        return fail(
            "ESP32 Wokwi RFC2217 port 4001 is not open. "
            "Start the root Wokwi simulation first.",
            2,
        )

    print("[PASS] ESP32 Wokwi endpoint is available on port 4001.")

    if not port_open(PICO_HOST, PICO_PORT):
        return fail(
            "Pico Wokwi RFC2217 port 4000 is not open. "
            "Start the pico/ Wokwi simulation in a second VS Code window.",
            3,
        )

    print("[PASS] Pico Wokwi endpoint is available on port 4000.")

    model_dir = ROOT / "ml" / "models"
    result_dir = ROOT / "ml" / "results"

    missing_models = [
        str(model_dir / name)
        for name in REQUIRED_MODELS
        if not (model_dir / name).is_file()
    ]

    missing_results = [
        str(result_dir / name)
        for name in REQUIRED_RESULTS
        if not (result_dir / name).is_file()
    ]

    missing = missing_models + missing_results

    if missing:
        print("[FAIL] Required local AI artifacts are missing:")
        for path in missing:
            print(f"  - {path}")
        print()
        print("The full AI bridge cannot produce genuine live AI values without them.")
        return 4

    print("[PASS] Required laptop AI artifacts are present.")

    runtime = ROOT / "bridge" / "runtime"
    runtime.mkdir(parents=True, exist_ok=True)

    stale_ai = runtime / "latest_ai_result.json"
    if stale_ai.exists():
        stale_ai.unlink()
        print("[PASS] Previous runtime AI snapshot cleared.")

    bridge = ROOT / "bridge" / "live_ai_bridge.py"
    if not bridge.is_file():
        return fail(f"Missing bridge: {bridge}", 5)

    print()
    print("Starting the existing closed-loop AI bridge...")
    print()
    print("IMPORTANT:")
    print("  - Stop abs-dashboard-wokwi-live-bridge.py before using this.")
    print("  - Keep BOTH Wokwi simulations running.")
    print("  - Keep dashboard npm server running separately.")
    print("  - AI is genuinely WAITING during 24-frame warmup.")
    print("  - After 24 accepted frames, live AI/Pico values appear.")
    print()

    command = [
        sys.executable,
        str(bridge),
        "--url",
        "rfc2217://localhost:4001",
        "--pico-url",
        "rfc2217://localhost:4000",
        "--network-simulator",
        "",
    ]

    try:
        completed = subprocess.run(
            command,
            cwd=ROOT,
            check=False,
        )
        return int(completed.returncode)
    except KeyboardInterrupt:
        print()
        print("Full live bridge stopped by user.")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
