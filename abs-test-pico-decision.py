import json
import time
import serial

PICO_URL = "rfc2217://localhost:4000"
PREFIX = "PICO_RECOMMEND|"

payload = {
    "schema": "pico.control.recommend.v1",
    "recommended_mode": "ECO",
    "recommended_power_source": "GRID",
    "generator_recommendation": "STOP",
    "fault_domain": "NORMAL",
    "reason": "DIRECT PICO DECISION TEST",
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

line = PREFIX + json.dumps(payload, separators=(",", ":")) + "\n"

print("Connecting to Pico:", PICO_URL)

port = serial.serial_for_url(
    PICO_URL,
    baudrate=115200,
    timeout=0.25,
)

try:
    time.sleep(0.5)
    port.reset_input_buffer()

    print("Sending valid PICO_RECOMMEND packet...")
    port.write(line.encode("utf-8"))
    port.flush()

    deadline = time.monotonic() + 5.0
    saw_decision = False

    print()
    print("--- RAW PICO OUTPUT ---")

    while time.monotonic() < deadline:
        raw = port.readline()

        if not raw:
            continue

        text = raw.decode(
            "utf-8",
            errors="replace"
        ).rstrip()

        if not text:
            continue

        print(text)

        if text.startswith("PICO_DECISION|"):
            saw_decision = True

    print("--- END RAW OUTPUT ---")
    print()

    if saw_decision:
        print("PASS: Pico control-decision handler replied.")
    else:
        print("FAIL: No PICO_DECISION reply was received.")

finally:
    port.close()
