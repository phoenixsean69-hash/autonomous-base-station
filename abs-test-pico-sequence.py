import json
import time
import serial
from pathlib import Path

PICO_URL = "rfc2217://localhost:4000"

packet_path = Path("bridge/captured_esp32_packet.txt")

if not packet_path.exists():
    raise SystemExit("Missing bridge/captured_esp32_packet.txt")

telemetry = packet_path.read_text(
    encoding="utf-8"
).strip()

payload = {
    "schema": "pico.control.recommend.v1",
    "recommended_mode": "ECO",
    "recommended_power_source": "GRID",
    "generator_recommendation": "STOP",
    "fault_domain": "NORMAL",
    "reason": "SEQUENTIAL BRIDGE TEST",
    "trust_decision": "ACCEPT",
    "trust_reason": "CLEAR_CLASS_SEPARATION",
    "current_operating_mode": "ECO",
    "domain_confidence": 0.98,
    "battery_soc_pct": 75.84,
    "traffic_load_pct": 34.21,
    "grid_available": True,
    "generator_running": False,
    "anomaly_flag": False,
}

recommendation = (
    "PICO_RECOMMEND|" +
    json.dumps(payload, separators=(",", ":")) +
    "\n"
)

port = serial.serial_for_url(
    PICO_URL,
    baudrate=115200,
    timeout=0.25
)

try:
    time.sleep(0.5)
    port.reset_input_buffer()

    print()
    print("STEP A: Sending ESP32 telemetry to Pico...")

    port.write((telemetry + "\n").encode("utf-8"))
    port.flush()

    deadline = time.monotonic() + 5
    got_result = False

    while time.monotonic() < deadline:
        raw = port.readline()

        if not raw:
            continue

        line = raw.decode(
            "utf-8",
            errors="replace"
        ).rstrip()

        if line:
            print(line)

        if line.startswith("PICO_RESULT|"):
            got_result = True
            break

    print()
    print("PICO_RESULT received:", got_result)

    print()
    print("STEP B: Sending control recommendation...")

    port.write(
        recommendation.encode("utf-8")
    )
    port.flush()

    deadline = time.monotonic() + 5
    got_decision = False

    while time.monotonic() < deadline:
        raw = port.readline()

        if not raw:
            continue

        line = raw.decode(
            "utf-8",
            errors="replace"
        ).rstrip()

        if line:
            print(line)

        if line.startswith("PICO_DECISION|"):
            got_decision = True
            break

    print()
    print("==============================")
    print("PICO_RESULT  :", got_result)
    print("PICO_DECISION:", got_decision)
    print("==============================")

finally:
    port.close()
