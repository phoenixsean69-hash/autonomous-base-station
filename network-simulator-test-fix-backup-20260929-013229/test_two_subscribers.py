from __future__ import annotations

import asyncio
import json
import os

import websockets

BASE = os.environ.get(
    "ABS_SIM_WS",
    "ws://127.0.0.1:8000",
)

A = "0712000001"
B = "0712000002"


async def recv_until(
    websocket,
    event_type: str,
):
    while True:
        message = json.loads(
            await websocket.recv()
        )

        print(
            f"[{event_type}]",
            json.dumps(
                message,
                indent=2,
            ),
        )

        if (
            message.get("type")
            == event_type
        ):
            return message


async def main():
    async with (
        websockets.connect(
            f"{BASE}/ws/{A}"
        ) as a,
        websockets.connect(
            f"{BASE}/ws/{B}"
        ) as b,
    ):
        await recv_until(
            a,
            "registered",
        )
        await recv_until(
            b,
            "registered",
        )

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

        ringing = await recv_until(
            a,
            "call.ringing",
        )
        incoming = await recv_until(
            b,
            "call.incoming",
        )

        call_id = (
            incoming["call"][
                "call_id"
            ]
        )

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
        )
        await recv_until(
            b,
            "call.connected",
        )

        for _ in range(5):
            await recv_until(
                a,
                "call.metrics",
            )

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
        )

        print()
        print(
            "[PASS] Two-subscriber "
            "call simulation completed."
        )


if __name__ == "__main__":
    asyncio.run(main())
