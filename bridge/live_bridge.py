import json
import sys
import time

try:
    import serial
except ImportError:
    print("PySerial missing.")
    print("Install with:")
    print("  py -m pip install pyserial")
    raise SystemExit(1)


ESP32_URL = "rfc2217://localhost:4001"
PICO_URL = "rfc2217://localhost:4000"

BAUD_RATE = 115200

ESP32_PREFIX = "ABS_JSON|"
PICO_PREFIX = "PICO_RESULT|"


def connect(name, url):
    print(f"Connecting to {name}: {url}")

    try:
        port = serial.serial_for_url(
            url,
            baudrate=BAUD_RATE,
            timeout=0.20
        )
    except Exception as exc:
        print()
        print(f"[ERROR] Could not connect to {name}.")
        print(exc)
        raise

    time.sleep(0.3)
    port.reset_input_buffer()

    print(f"[OK] {name} connected.")
    return port


def wait_for_pico_result(pico, timeout=3.0):
    deadline = time.time() + timeout

    while time.time() < deadline:
        raw = pico.readline()

        if not raw:
            continue

        line = raw.decode(
            "utf-8",
            errors="replace"
        ).strip()

        if not line:
            continue

        # Show Pico diagnostic output while waiting.
        print(f"  PICO > {line}")

        if line.startswith(PICO_PREFIX):
            return line

    return None


def main():
    print()
    print("==========================================================")
    print(" AUTONOMOUS BASE STATION - LIVE TELEMETRY BRIDGE")
    print("==========================================================")
    print()
    print("ESP32 -> Laptop -> Pico")
    print()

    try:
        esp32 = connect(
            "ESP32",
            ESP32_URL
        )

        pico = connect(
            "Pico",
            PICO_URL
        )

    except Exception:
        print()
        print("Required:")
        print("  ESP32 Wokwi running on port 4001")
        print("  Pico Wokwi running on port 4000")
        return 1

    forwarded = 0
    accepted = 0
    rejected = 0

    print()
    print("==========================================================")
    print(" LIVE BRIDGE RUNNING")
    print("==========================================================")
    print()
    print("Waiting for ABS_JSON telemetry...")
    print("Press Ctrl+C to stop.")
    print()

    try:
        while True:
            raw = esp32.readline()

            if not raw:
                continue

            line = raw.decode(
                "utf-8",
                errors="replace"
            ).strip()

            if not line.startswith(ESP32_PREFIX):
                continue

            json_text = line[len(ESP32_PREFIX):]

            # Validate that ESP32 sent syntactically valid JSON
            # before forwarding it to the Pico.
            try:
                telemetry = json.loads(json_text)
            except json.JSONDecodeError as exc:
                rejected += 1

                print()
                print(
                    f"[DROP] Invalid ESP32 JSON: {exc}"
                )
                continue

            schema = telemetry.get(
                "schema",
                "UNKNOWN"
            )

            fault = telemetry.get(
                "fault_label",
                "UNKNOWN"
            )

            mode = telemetry.get(
                "operating_mode",
                "UNKNOWN"
            )

            pa_temp = telemetry.get(
                "pa_temp_c",
                None
            )

            latency = telemetry.get(
                "latency_ms",
                None
            )

            forwarded += 1

            print()
            print(
                "----------------------------------------------------------"
            )

            print(
                f"[ESP32 PACKET #{forwarded}]"
            )

            print(
                f"Schema       : {schema}"
            )

            print(
                f"Ground Truth : {fault}"
            )

            print(
                f"Energy Mode  : {mode}"
            )

            if pa_temp is not None:
                print(
                    f"PA Temp      : {pa_temp:.2f} C"
                )

            if latency is not None:
                print(
                    f"Latency      : {latency:.2f} ms"
                )

            # Forward the exact ESP32 machine-readable packet.
            pico.write(
                (
                    line +
                    "\n"
                ).encode(
                    "utf-8"
                )
            )

            pico.flush()

            print(
                "[FORWARD] ESP32 -> Pico"
            )

            result_line = wait_for_pico_result(
                pico
            )

            if result_line is None:
                rejected += 1

                print(
                    "[FAIL] Pico returned no PICO_RESULT."
                )

                continue

            result_json_text = result_line[
                len(PICO_PREFIX):
            ]

            try:
                result = json.loads(
                    result_json_text
                )
            except json.JSONDecodeError as exc:
                rejected += 1

                print(
                    f"[FAIL] Invalid Pico JSON: {exc}"
                )

                continue

            if result.get(
                "telemetry_ok"
            ) is True:

                accepted += 1

                print(
                    "[PASS] Pico accepted live ESP32 telemetry."
                )

            else:
                rejected += 1

                print(
                    "[REJECTED] Pico rejected telemetry."
                )

            print(
                f"Forwarded={forwarded} "
                f"Accepted={accepted} "
                f"Rejected={rejected}"
            )

    except KeyboardInterrupt:
        print()
        print()
        print("Bridge stopped by user.")

    finally:
        esp32.close()
        pico.close()

    print()
    print("==========================================================")
    print(" FINAL BRIDGE STATISTICS")
    print("==========================================================")
    print(
        f"Forwarded : {forwarded}"
    )
    print(
        f"Accepted  : {accepted}"
    )
    print(
        f"Rejected  : {rejected}"
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())