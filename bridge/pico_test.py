import sys
import time

try:
    import serial
except ImportError:
    print()
    print("[ERROR] PySerial is not installed.")
    print()
    print("Install it with:")
    print("  py -m pip install pyserial")
    print()
    sys.exit(1)


PICO_URL = "rfc2217://localhost:4000"
BAUD_RATE = 115200


def main():
    print()
    print("==================================================")
    print(" AUTONOMOUS BASE STATION - LAPTOP/PICO TEST")
    print("==================================================")
    print()
    print(f"Connecting to Pico at {PICO_URL} ...")

    try:
        pico = serial.serial_for_url(
            PICO_URL,
            baudrate=BAUD_RATE,
            timeout=0.25
        )
    except Exception as exc:
        print()
        print("[ERROR] Could not connect to the Pico simulation.")
        print()
        print(exc)
        print()
        print("Make sure:")
        print("  1. pico/ is open in VS Code")
        print("  2. Wokwi Pico simulation is RUNNING")
        print("  3. Port 4000 is not already in use")
        return 1

    try:
        time.sleep(0.5)

        # Clear any boot text waiting in the receive buffer.
        pico.reset_input_buffer()

        print("[OK] Connected.")
        print()
        print("Sending DEMO command...")
        print()

        pico.write(b"DEMO\n")
        pico.flush()

        deadline = time.time() + 8.0
        result_received = False

        while time.time() < deadline:
            raw = pico.readline()

            if not raw:
                continue

            line = raw.decode(
                "utf-8",
                errors="replace"
            ).rstrip()

            if not line:
                continue

            print(line)

            if line.startswith("PICO_RESULT|"):
                result_received = True
                break

        print()

        if result_received:
            print("==================================================")
            print(" PASS - LAPTOP COMMUNICATED WITH PICO")
            print("==================================================")
            return 0

        print("==================================================")
        print(" FAIL - NO PICO_RESULT RECEIVED")
        print("==================================================")
        return 2

    finally:
        pico.close()


if __name__ == "__main__":
    raise SystemExit(main())