import json
import time
import serial
from pathlib import Path

result_file = Path("bridge/runtime/latest_ai_result.json")

data = json.loads(
    result_file.read_text(encoding="utf-8")
)

recommendation = data["runtime"]["pico_recommendation"]

payload = (
    "PICO_RECOMMEND|" +
    json.dumps(recommendation, separators=(",", ":")) +
    "\n"
).encode("utf-8")

print("Recommendation loaded from latest_ai_result.json")
print()
print(json.dumps(recommendation, indent=2))
print()
print("Payload bytes:", len(payload))

port = serial.serial_for_url(
    "rfc2217://localhost:4000",
    baudrate=115200,
    timeout=0.25
)

try:
    time.sleep(0.5)
    port.reset_input_buffer()

    print()
    print("Sending using SAME 64-byte chunking as live bridge...")

    started = time.monotonic()

    for start in range(0, len(payload), 64):
        port.write(payload[start:start + 64])
        port.flush()
        time.sleep(0.003)

    deadline = time.monotonic() + 10.0

    print()
    print("--- PICO OUTPUT ---")

    while time.monotonic() < deadline:
        raw = port.readline()

        if not raw:
            continue

        line = raw.decode(
            "utf-8",
            errors="replace"
        ).rstrip()

        if not line:
            continue

        elapsed = time.monotonic() - started

        print(f"[{elapsed:.3f}s] {line}")

        if line.startswith("PICO_DECISION|"):
            print()
            print("PASS")
            print(
                f"Pico decision latency = {elapsed:.3f} seconds"
            )
            break
    else:
        print()
        print("FAIL: No PICO_DECISION within 10 seconds")

finally:
    port.close()
