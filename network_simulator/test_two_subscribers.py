from __future__ import annotations

import asyncio
import json
import os
import sys

import websockets

BASE = os.environ.get(
    "ABS_SIM_WS",
    "ws://127.0.0.1:8000",
)

A = "0712000001"
B = "0712000002"

EVENT_TIMEOUT_SECONDS = 6.0


async def recv_until(
    websocket,
    event_type: str,
    who: str,
):
    while True:
        try:
            raw = await asyncio.wait_for(
                websocket.recv(),
                timeout=EVENT_TIMEOUT_SECONDS,
            )
        except asyncio.TimeoutError as exc:
            raise RuntimeError(
                f"Timed out waiting for {event_type} on {who}. "
                "Check the network_simulator server terminal for errors."
            ) from exc

        message = json.loads(raw)
        actual = message.get("type", "unknown")

        print(
            f"[{who}] {actual}",
            json.dumps(
                message,
                indent=2,
            ),
        )

        if actual == event_type:
            return message


async def main():
    print()
    print("ABS TWO-SUBSCRIBER NETWORK TEST")
    print(f"Server: {BASE}")
    print()

    async with (
        websockets.connect(
            f"{BASE}/ws/{A}",
            ping_interval=10,
            ping_timeout=10,
        ) as a,
        websockets.connect(
            f"{BASE}/ws/{B}",
            ping_interval=10,
            ping_timeout=10,
        ) as b,
    ):
        await recv_until(
            a,
            "registered",
            "A",
        )
        await recv_until(
            b,
            "registered",
            "B",
        )

        print()
        print("[STEP 1] Subscriber A calls Subscriber B")

        await a.send(
            json.dumps(
                {
                    "type":
                        "call.start",
                    "to":
                        "0712 000 002",
                }
            )
        )

        await recv_until(
            a,
            "call.ringing",
            "A",
        )
        incoming = await recv_until(
            b,
            "call.incoming",
            "B",
        )

        call_id = (
            incoming["call"][
                "call_id"
            ]
        )

        print()
        print("[STEP 2] Subscriber B answers")

        await b.send(
            json.dumps(
                {
                    "type":
                        "call.answer",
                    "call_id":
                        call_id,
                }
            )
        )

        await recv_until(
            a,
            "call.connected",
            "A",
        )
        await recv_until(
            b,
            "call.connected",
            "B",
        )

        print()
        print("[STEP 3] Collecting three live network samples")

        for sample in range(1, 4):
            print(
                f"[WAIT] network sample {sample}/3 ..."
            )

            message = await recv_until(
                a,
                "call.metrics",
                "A",
            )

            metrics = message["call"]["metrics"]

            print(
                "[SAMPLE] "
                f"latency={metrics['latency_ms']} ms | "
                f"jitter={metrics['jitter_ms']} ms | "
                f"loss={metrics['packet_loss_pct']}% | "
                f"rssi={metrics['rssi_dbm']} dBm | "
                f"quality={metrics['quality']}"
            )

        print()
        print("[STEP 4] Ending call")

        await a.send(
            json.dumps(
                {
                    "type":
                        "call.end",
                    "call_id":
                        call_id,
                }
            )
        )

        await recv_until(
            a,
            "call.ended",
            "A",
        )

    print()
    print("[PASS] Two-subscriber call simulation completed.")
    print("       Signaling + packet metrics are working.")
    print()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print()
        print("[STOPPED] Test interrupted with Ctrl+C.")
        sys.exit(130)
    except Exception as exc:
        print()
        print(f"[FAIL] {exc}")
        sys.exit(1)
