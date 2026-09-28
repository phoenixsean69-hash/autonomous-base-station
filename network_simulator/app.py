from __future__ import annotations

import asyncio
import json
import uuid
from contextlib import suppress
from typing import Dict, Optional

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

from engine import NetworkEngine
from models import CallSession, CallState, FaultState

APP_TITLE = "ABS Mobile Network Simulator"

SUBSCRIBERS = {
    "0712000001": {
        "display": "0712 000 001",
        "name": "Subscriber A",
    },
    "0712000002": {
        "display": "0712 000 002",
        "name": "Subscriber B",
    },
}

app = FastAPI(
    title=APP_TITLE,
    version="0.1.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)


class FaultPatch(BaseModel):
    congestion: Optional[bool] = None
    impairment: Optional[bool] = None
    link_failure: Optional[bool] = None
    upstream_failure: Optional[bool] = None
    path_loss: Optional[bool] = None
    interference: Optional[bool] = None
    antenna_mismatch: Optional[bool] = None
    radio_failure: Optional[bool] = None


def normalize_number(value: str) -> str:
    return "".join(
        character
        for character in value
        if character.isdigit()
    )


class SimulatorState:
    def __init__(self) -> None:
        self.connections: Dict[str, WebSocket] = {}
        self.calls: Dict[str, CallSession] = {}
        self.faults = FaultState()
        self.engine = NetworkEngine()
        self.metric_tasks: Dict[str, asyncio.Task] = {}
        self.lock = asyncio.Lock()

    async def send(
        self,
        number: str,
        payload: dict,
    ) -> None:
        websocket = self.connections.get(number)
        if websocket is None:
            return

        with suppress(Exception):
            await websocket.send_json(payload)

    async def broadcast(
        self,
        payload: dict,
    ) -> None:
        for number in list(self.connections):
            await self.send(number, payload)

    def active_call_for(
        self,
        number: str,
    ) -> Optional[CallSession]:
        for call in self.calls.values():
            if (
                call.involves(number)
                and call.state
                in (
                    CallState.RINGING,
                    CallState.CONNECTED,
                )
            ):
                return call
        return None

    def connected_call_count(self) -> int:
        return sum(
            1
            for call in self.calls.values()
            if call.state == CallState.CONNECTED
        )


state = SimulatorState()


async def send_subscriber_status() -> None:
    await state.broadcast(
        {
            "type": "subscribers.status",
            "subscribers": [
                {
                    "number": data["display"],
                    "name": data["name"],
                    "online": number
                    in state.connections,
                }
                for number, data in SUBSCRIBERS.items()
            ],
        }
    )


async def metric_loop(call_id: str) -> None:
    try:
        while True:
            await asyncio.sleep(1.0)

            call = state.calls.get(call_id)
            if (
                call is None
                or call.state
                != CallState.CONNECTED
            ):
                return

            active_calls = max(
                1,
                state.connected_call_count(),
            )

            state.engine.step(
                call.metrics,
                state.faults,
                active_calls,
            )

            payload = {
                "type": "call.metrics",
                "call": call.as_dict(),
                "faults": state.faults.as_dict(),
            }

            await state.send(
                call.caller,
                payload,
            )
            await state.send(
                call.callee,
                payload,
            )

            if call.metrics.quality == "FAILED":
                await end_call(
                    call,
                    reason="NETWORK_FAILURE",
                )
                return
    except asyncio.CancelledError:
        raise


async def end_call(
    call: CallSession,
    reason: str,
) -> None:
    if call.state in (
        CallState.ENDED,
        CallState.REJECTED,
        CallState.FAILED,
    ):
        return

    call.state = (
        CallState.FAILED
        if reason == "NETWORK_FAILURE"
        else CallState.ENDED
    )
    call.ended_at_monotonic = (
        asyncio.get_running_loop().time()
    )

    payload = {
        "type": "call.ended",
        "reason": reason,
        "call": call.as_dict(),
    }

    await state.send(
        call.caller,
        payload,
    )
    await state.send(
        call.callee,
        payload,
    )

    task = state.metric_tasks.pop(
        call.call_id,
        None,
    )

    if task is not None:
        task.cancel()


async def handle_call_start(
    source: str,
    message: dict,
) -> None:
    destination = normalize_number(
        str(message.get("to", ""))
    )

    if destination not in SUBSCRIBERS:
        await state.send(
            source,
            {
                "type": "call.failed",
                "reason": "UNKNOWN_SUBSCRIBER",
            },
        )
        return

    if destination == source:
        await state.send(
            source,
            {
                "type": "call.failed",
                "reason": "CANNOT_CALL_SELF",
            },
        )
        return

    if state.active_call_for(source):
        await state.send(
            source,
            {
                "type": "call.failed",
                "reason": "CALLER_BUSY",
            },
        )
        return

    if state.active_call_for(destination):
        await state.send(
            source,
            {
                "type": "call.failed",
                "reason": "CALLEE_BUSY",
            },
        )
        return

    if destination not in state.connections:
        await state.send(
            source,
            {
                "type": "call.failed",
                "reason": "CALLEE_OFFLINE",
            },
        )
        return

    call = CallSession(
        call_id=str(uuid.uuid4()),
        caller=source,
        callee=destination,
    )
    state.calls[call.call_id] = call

    await state.send(
        source,
        {
            "type": "call.ringing",
            "call": call.as_dict(),
        },
    )

    await state.send(
        destination,
        {
            "type": "call.incoming",
            "call": call.as_dict(),
        },
    )


async def handle_call_answer(
    source: str,
    message: dict,
) -> None:
    call_id = str(
        message.get("call_id", "")
    )
    call = state.calls.get(call_id)

    if (
        call is None
        or call.callee != source
        or call.state != CallState.RINGING
    ):
        await state.send(
            source,
            {
                "type": "call.failed",
                "reason": "INVALID_ANSWER",
            },
        )
        return

    call.state = CallState.CONNECTED
    call.connected_at_monotonic = (
        asyncio.get_running_loop().time()
    )

    payload = {
        "type": "call.connected",
        "call": call.as_dict(),
    }

    await state.send(
        call.caller,
        payload,
    )
    await state.send(
        call.callee,
        payload,
    )

    state.metric_tasks[call.call_id] = (
        asyncio.create_task(
            metric_loop(call.call_id)
        )
    )


async def handle_call_reject(
    source: str,
    message: dict,
) -> None:
    call_id = str(
        message.get("call_id", "")
    )
    call = state.calls.get(call_id)

    if (
        call is None
        or call.callee != source
        or call.state != CallState.RINGING
    ):
        return

    call.state = CallState.REJECTED

    payload = {
        "type": "call.rejected",
        "call": call.as_dict(),
    }

    await state.send(
        call.caller,
        payload,
    )
    await state.send(
        call.callee,
        payload,
    )


async def handle_call_end(
    source: str,
    message: dict,
) -> None:
    call_id = str(
        message.get("call_id", "")
    )
    call = state.calls.get(call_id)

    if (
        call is None
        or not call.involves(source)
    ):
        return

    await end_call(
        call,
        reason="USER_ENDED",
    )


async def handle_ws_message(
    source: str,
    message: dict,
) -> None:
    event_type = message.get("type")

    if event_type == "call.start":
        await handle_call_start(
            source,
            message,
        )
        return

    if event_type == "call.answer":
        await handle_call_answer(
            source,
            message,
        )
        return

    if event_type == "call.reject":
        await handle_call_reject(
            source,
            message,
        )
        return

    if event_type == "call.end":
        await handle_call_end(
            source,
            message,
        )
        return

    if event_type == "ping":
        await state.send(
            source,
            {
                "type": "pong",
            },
        )
        return

    await state.send(
        source,
        {
            "type": "error",
            "message": f"Unknown event: {event_type}",
        },
    )


@app.get("/")
async def root() -> dict:
    return {
        "service": APP_TITLE,
        "status": "running",
        "websocket": "/ws/{subscriber_number}",
        "subscribers": list(
            subscriber["display"]
            for subscriber in SUBSCRIBERS.values()
        ),
    }


@app.get("/status")
async def status() -> dict:
    return {
        "online_subscribers": [
            SUBSCRIBERS[number]["display"]
            for number in state.connections
            if number in SUBSCRIBERS
        ],
        "faults": state.faults.as_dict(),
        "calls": [
            call.as_dict()
            for call in state.calls.values()
        ],
    }


@app.get("/faults")
async def get_faults() -> dict:
    return state.faults.as_dict()


@app.patch("/faults")
async def patch_faults(
    patch: FaultPatch,
) -> dict:
    updates = patch.model_dump(
        exclude_none=True
    )

    for key, value in updates.items():
        setattr(
            state.faults,
            key,
            value,
        )

    payload = {
        "type": "faults.updated",
        "faults": state.faults.as_dict(),
    }
    await state.broadcast(payload)

    return payload


@app.post("/faults/reset")
async def reset_faults() -> dict:
    state.faults = FaultState()

    payload = {
        "type": "faults.updated",
        "faults": state.faults.as_dict(),
    }
    await state.broadcast(payload)

    return payload


@app.websocket(
    "/ws/{subscriber_number}"
)
async def websocket_endpoint(
    websocket: WebSocket,
    subscriber_number: str,
) -> None:
    number = normalize_number(
        subscriber_number
    )

    if number not in SUBSCRIBERS:
        await websocket.close(
            code=1008,
            reason="Unknown subscriber",
        )
        return

    await websocket.accept()

    old = state.connections.get(number)
    if old is not None:
        with suppress(Exception):
            await old.close(
                code=1012,
                reason="Replaced by new connection",
            )

    state.connections[number] = websocket

    await websocket.send_json(
        {
            "type": "registered",
            "subscriber": {
                "number": SUBSCRIBERS[number][
                    "display"
                ],
                "name": SUBSCRIBERS[number][
                    "name"
                ],
            },
        }
    )

    await send_subscriber_status()

    try:
        while True:
            raw = await websocket.receive_text()

            try:
                message = json.loads(raw)
            except json.JSONDecodeError:
                await websocket.send_json(
                    {
                        "type": "error",
                        "message": "Expected JSON message",
                    }
                )
                continue

            await handle_ws_message(
                number,
                message,
            )

    except WebSocketDisconnect:
        pass

    finally:
        if (
            state.connections.get(number)
            is websocket
        ):
            state.connections.pop(
                number,
                None,
            )

        active_call = state.active_call_for(
            number
        )

        if active_call is not None:
            await end_call(
                active_call,
                reason="SUBSCRIBER_DISCONNECTED",
            )

        await send_subscriber_status()
