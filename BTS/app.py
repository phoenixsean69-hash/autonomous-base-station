from __future__ import annotations

import asyncio
import json
import random
import time
import uuid
import serial
from dataclasses import dataclass, field
from typing import Dict, Optional

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware

BTS_ID = "BTS-001"
STARTED_AT = time.time()

BTS_HARDWARE_URL = "rfc2217://localhost:4010"
BTS_HARDWARE_PREFIX = "BTS_HW|"
BTS_HARDWARE_SCHEMA = "bts.hw.v1"
HARDWARE_STALE_MS = 30000

app = FastAPI(title="Basic BTS Simulator", version="0.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)


@dataclass
class Subscriber:
    number: str
    socket: WebSocket
    connected_at: float = field(default_factory=time.time)


subscribers: Dict[str, Subscriber] = {}
calls: Dict[str, dict] = {}
sms_log: list[dict] = []

network = {
    "traffic_load_pct": 0.0,
    "latency_ms": 40.0,
    "packet_loss_pct": 0.5,
    "jitter_ms": 0.0,
    "rssi_dbm": -67.0,
    "radio_temperature_c": 42.0,
    "radio_operational": True,
    "backhaul_available": True,
    "link_up": True,
    "upstream_reachable": True,
    "cabinet_temp_c": None,
    "humidity_pct": None,
    "rf_forward_w": None,
    "rf_reflected_w": None,
    "reflection_ratio": None,
    "fan_operational": None,
    "interference": None,
    "radio_fault": None,
    "alarm": None,
    "hardware_last_seen_ms": None,
    "hardware_timestamp_ms": None,
    "hardware_error": None,
}


def now_ms() -> int:
    return int(time.time() * 1000)


def active_call_count() -> int:
    return sum(
        1
        for call in calls.values()
        if call.get("state") in {"RINGING", "CONNECTED"}
    )


def hardware_age_ms() -> Optional[int]:
    last_seen = network.get("hardware_last_seen_ms")
    if last_seen is None:
        return None
    return max(0, now_ms() - int(last_seen))


def hardware_fresh() -> bool:
    age = hardware_age_ms()
    return age is not None and age <= HARDWARE_STALE_MS


def traffic_load_pct() -> float:
    if hardware_fresh():
        return float(network["traffic_load_pct"])
    return min(100.0, active_call_count() * 18.0)


def state_snapshot() -> dict:
    return {
        "schema": "bts.telemetry.v1",
        "bts_id": BTS_ID,
        "timestamp_ms": now_ms(),
        "uptime_s": round(time.time() - STARTED_AT, 1),
        "active_subscribers": len(subscribers),
        # BTS DASHBOARD MOBILE PRESENCE V1
        "subscriber_numbers": sorted(subscribers.keys()),
        "active_call_pairs": [
            {
                "caller": call.get("caller"),
                "callee": call.get("callee"),
                "state": call.get("state"),
            }
            for call in calls.values()
            if call.get("state") in {"RINGING", "CONNECTED"}
        ],
        "active_calls": active_call_count(),
        "sms_total": len(sms_log),
        "traffic_load_pct": round(traffic_load_pct(), 1),
        "latency_ms": round(network["latency_ms"], 1),
        "packet_loss_pct": round(network["packet_loss_pct"], 2),
        "jitter_ms": round(network["jitter_ms"], 1),
        "rssi_dbm": round(network["rssi_dbm"], 1),
        "radio_temperature_c": round(network["radio_temperature_c"], 1),
        "radio_operational": bool(network["radio_operational"]),
        "backhaul_available": bool(network["backhaul_available"]),
        "link_up": bool(network["link_up"]),
        "upstream_reachable": bool(network["upstream_reachable"]),
        "hardware_connected": hardware_fresh(),
        "hardware_age_ms": hardware_age_ms(),
        "hardware_source": "RFC2217" if hardware_fresh() else "SOFTWARE_FALLBACK",
        "hardware_timestamp_ms": network["hardware_timestamp_ms"],
        "cabinet_temp_c": network["cabinet_temp_c"],
        "humidity_pct": network["humidity_pct"],
        "rf_forward_w": network["rf_forward_w"],
        "rf_reflected_w": network["rf_reflected_w"],
        "reflection_ratio": network["reflection_ratio"],
        "fan_operational": network["fan_operational"],
        "interference": network["interference"],
        "radio_fault": network["radio_fault"],
        "alarm": network["alarm"],
        "hardware_error": network["hardware_error"],
    }


async def send_json(ws: WebSocket, payload: dict) -> None:
    await ws.send_text(json.dumps(payload, separators=(",", ":")))


async def send_to(number: str, payload: dict) -> bool:
    subscriber = subscribers.get(number)
    if subscriber is None:
        return False

    try:
        await send_json(subscriber.socket, payload)
        return True
    except Exception:
        subscribers.pop(number, None)
        return False


async def broadcast_status() -> None:
    payload = {
        "type": "subscribers.status",
        "subscribers": [
            {
                "number": number,
                "online": True,
            }
            for number in sorted(subscribers.keys())
        ],
        "timestamp_ms": now_ms(),
    }

    dead = []

    for number, subscriber in list(subscribers.items()):
        try:
            await send_json(subscriber.socket, payload)
        except Exception:
            dead.append(number)

    for number in dead:
        subscribers.pop(number, None)


def find_call_for(number: str) -> Optional[dict]:
    for call in calls.values():
        if (
            call.get("state") in {"RINGING", "CONNECTED"}
            and number in {call.get("caller"), call.get("callee")}
        ):
            return call
    return None


def call_metrics_snapshot() -> dict:
    snapshot = state_snapshot()

    latency = float(snapshot["latency_ms"])
    jitter = float(snapshot["jitter_ms"])
    packet_loss = float(snapshot["packet_loss_pct"])
    rssi = float(snapshot["rssi_dbm"])
    radio_ok = bool(snapshot["radio_operational"])
    backhaul_ok = bool(snapshot["backhaul_available"])

    if not radio_ok or not backhaul_ok:
        quality = "FAILED"
    elif packet_loss >= 5.0 or latency >= 250.0 or rssi < -100.0:
        quality = "POOR"
    elif packet_loss >= 1.0 or jitter >= 15.0 or latency >= 100.0 or rssi < -85.0:
        quality = "DEGRADED"
    else:
        quality = "GOOD"

    return {
        "packets_sent": 0,
        "packets_received": 0,
        "packets_dropped": 0,
        "latency_ms": round(latency, 1),
        "jitter_ms": round(jitter, 1),
        "packet_loss_pct": round(packet_loss, 2),
        "rssi_dbm": round(rssi, 1),
        "traffic_load_pct": round(float(snapshot["traffic_load_pct"]), 1),
        "quality": quality,
    }


def call_view(call: dict) -> dict:
    connected_ms = call.get("connected_ms")
    ended_ms = call.get("ended_ms")

    duration_s = 0

    if connected_ms is not None:
        end_ms = ended_ms if ended_ms is not None else now_ms()
        duration_s = max(0, int((int(end_ms) - int(connected_ms)) / 1000))

    return {
        "call_id": str(call["call_id"]),
        "caller": str(call["caller"]),
        "callee": str(call["callee"]),
        "state": str(call["state"]),
        "duration_s": duration_s,
        "metrics": call_metrics_snapshot(),
    }


async def handle_call_start(number: str, message: dict) -> None:
    destination = str(message.get("to", "")).strip()

    if not destination:
        await send_to(number, {
            "type": "call.failed",
            "reason": "MISSING_DESTINATION",
            "timestamp_ms": now_ms(),
        })
        return

    if destination == number:
        await send_to(number, {
            "type": "call.failed",
            "reason": "CANNOT_CALL_SELF",
            "timestamp_ms": now_ms(),
        })
        return

    if not network["radio_operational"]:
        await send_to(number, {
            "type": "call.failed",
            "reason": "BTS_RADIO_UNAVAILABLE",
            "timestamp_ms": now_ms(),
        })
        return

    if destination not in subscribers:
        await send_to(number, {
            "type": "call.failed",
            "reason": "SUBSCRIBER_UNAVAILABLE",
            "to": destination,
            "timestamp_ms": now_ms(),
        })
        return

    if find_call_for(number):
        await send_to(number, {
            "type": "call.failed",
            "reason": "CALLER_BUSY",
            "timestamp_ms": now_ms(),
        })
        return

    if find_call_for(destination):
        await send_to(number, {
            "type": "call.failed",
            "reason": "CALLEE_BUSY",
            "timestamp_ms": now_ms(),
        })
        return

    call_id = str(uuid.uuid4())

    call = {
        "call_id": call_id,
        "caller": number,
        "callee": destination,
        "state": "RINGING",
        "started_ms": now_ms(),
        "connected_ms": None,
        "ended_ms": None,
    }

    calls[call_id] = call

    await send_to(number, {
        "type": "call.ringing",
        "call": call_view(call),
    })

    await send_to(destination, {
        "type": "call.incoming",
        "call": call_view(call),
    })


async def handle_call_answer(number: str, message: dict) -> None:
    call_id = str(message.get("call_id", "")).strip()
    call = calls.get(call_id)

    if not call or call.get("callee") != number or call.get("state") != "RINGING":
        await send_to(number, {
            "type": "call.failed",
            "reason": "INVALID_ANSWER",
            "timestamp_ms": now_ms(),
        })
        return

    call["state"] = "CONNECTED"
    call["connected_ms"] = now_ms()

    payload = {
        "type": "call.connected",
        "call": call_view(call),
    }

    await send_to(call["caller"], payload)
    await send_to(call["callee"], payload)


async def handle_call_reject(number: str, message: dict) -> None:
    call_id = str(message.get("call_id", "")).strip()
    call = calls.get(call_id)

    if not call or call.get("callee") != number:
        return

    call["state"] = "REJECTED"
    call["ended_ms"] = now_ms()

    payload = {
        "type": "call.rejected",
        "call": call_view(call),
    }

    await send_to(call["caller"], payload)
    await send_to(call["callee"], payload)


async def handle_call_end(number: str, message: dict) -> None:
    call_id = str(message.get("call_id", "")).strip()
    call = calls.get(call_id)

    if not call or number not in {call.get("caller"), call.get("callee")}:
        return

    call["state"] = "ENDED"
    call["ended_ms"] = now_ms()

    payload = {
        "type": "call.ended",
        "reason": "USER_ENDED",
        "call": call_view(call),
    }

    await send_to(call["caller"], payload)
    await send_to(call["callee"], payload)


async def handle_sms_send(number: str, message: dict) -> None:
    destination = str(message.get("to", "")).strip()
    body = str(message.get("body", ""))

    if not destination or not body:
        await send_to(number, {
            "type": "sms.failed",
            "reason": "MISSING_DESTINATION_OR_BODY",
            "timestamp_ms": now_ms(),
        })
        return

    sms_id = str(uuid.uuid4())

    record = {
        "sms_id": sms_id,
        "from": number,
        "to": destination,
        "body": body,
        "timestamp_ms": now_ms(),
        "status": "DELIVERED" if destination in subscribers else "QUEUED",
    }

    sms_log.append(record)

    if destination in subscribers:
        await send_to(destination, {"type": "sms.incoming", **record})

    await send_to(number, {"type": "sms.status", **record})



# BTS WEBRTC SIGNAL RELAY V1
async def handle_webrtc_signal(number: str, message: dict) -> None:
    event_type = str(message.get("type", "")).strip()
    call_id = str(message.get("call_id", "")).strip()

    if event_type not in {"webrtc.offer", "webrtc.answer", "webrtc.ice"}:
        await send_to(number, {
            "type": "webrtc.error",
            "reason": "UNSUPPORTED_SIGNAL",
            "timestamp_ms": now_ms(),
        })
        return

    if not call_id:
        await send_to(number, {
            "type": "webrtc.error",
            "reason": "MISSING_CALL_ID",
            "timestamp_ms": now_ms(),
        })
        return

    call = calls.get(call_id)

    if call is None:
        await send_to(number, {
            "type": "webrtc.error",
            "reason": "CALL_NOT_FOUND",
            "call_id": call_id,
            "timestamp_ms": now_ms(),
        })
        return

    if number not in {call.get("caller"), call.get("callee")}:
        await send_to(number, {
            "type": "webrtc.error",
            "reason": "NOT_CALL_PARTICIPANT",
            "call_id": call_id,
            "timestamp_ms": now_ms(),
        })
        return

    if call.get("state") != "CONNECTED":
        await send_to(number, {
            "type": "webrtc.error",
            "reason": "CALL_NOT_CONNECTED",
            "call_id": call_id,
            "timestamp_ms": now_ms(),
        })
        return

    peer = (
        call.get("callee")
        if number == call.get("caller")
        else call.get("caller")
    )

    if not peer or peer not in subscribers:
        await send_to(number, {
            "type": "webrtc.error",
            "reason": "PEER_UNAVAILABLE",
            "call_id": call_id,
            "timestamp_ms": now_ms(),
        })
        return

    payload = {
        "type": event_type,
        "call_id": call_id,
        "from": number,
        "timestamp_ms": now_ms(),
    }

    if event_type in {"webrtc.offer", "webrtc.answer"}:
        sdp = message.get("sdp")

        if not isinstance(sdp, dict):
            await send_to(number, {
                "type": "webrtc.error",
                "reason": "INVALID_SDP",
                "call_id": call_id,
                "timestamp_ms": now_ms(),
            })
            return

        payload["sdp"] = sdp

    if event_type == "webrtc.ice":
        candidate = message.get("candidate")

        if not isinstance(candidate, dict):
            await send_to(number, {
                "type": "webrtc.error",
                "reason": "INVALID_ICE",
                "call_id": call_id,
                "timestamp_ms": now_ms(),
            })
            return

        payload["candidate"] = candidate

    delivered = await send_to(peer, payload)

    if not delivered:
        await send_to(number, {
            "type": "webrtc.error",
            "reason": "PEER_UNAVAILABLE",
            "call_id": call_id,
            "timestamp_ms": now_ms(),
        })


async def handle_message(number: str, message: dict) -> None:
    event_type = str(message.get("type", ""))

    if event_type == "ping":
        await send_to(number, {
            "type": "pong",
            "timestamp_ms": now_ms(),
        })
    elif event_type == "call.start":
        await handle_call_start(number, message)
    elif event_type == "call.answer":
        await handle_call_answer(number, message)
    elif event_type == "call.reject":
        await handle_call_reject(number, message)
    elif event_type == "call.end":
        await handle_call_end(number, message)
    elif event_type in {"webrtc.offer", "webrtc.answer", "webrtc.ice"}:
        await handle_webrtc_signal(number, message)
    elif event_type == "sms.send":
        await handle_sms_send(number, message)
    else:
        await send_to(number, {
            "type": "error",
            "reason": "UNKNOWN_EVENT",
            "received_type": event_type,
            "timestamp_ms": now_ms(),
        })


@app.get("/health")
async def health():
    return {
        "ok": True,
        "service": "BTS",
        "bts_id": BTS_ID,
        "schema": "bts.health.v1",
    }


@app.get("/state")
async def state():
    return {
        "telemetry": state_snapshot(),
        "subscribers": sorted(subscribers.keys()),
        "calls": list(calls.values()),
        "sms": sms_log[-50:],
    }


@app.get("/telemetry")
async def telemetry():
    return state_snapshot()


@app.websocket("/ws/{subscriber_number}")
async def subscriber_socket(websocket: WebSocket, subscriber_number: str):
    number = subscriber_number.strip()
    await websocket.accept()

    # BTS SUBSCRIBER NUMBER LOCK V1
    # First claimant keeps the number. A later device is rejected.
    existing = subscribers.get(number)
    if existing is not None:
        await send_json(websocket, {
            "type": "registration.failed",
            "reason": "NUMBER_TAKEN",
            "subscriber": number,
            "bts_id": BTS_ID,
            "timestamp_ms": now_ms(),
        })
        await websocket.close(code=4409)
        return

    subscribers[number] = Subscriber(number=number, socket=websocket)

    await send_json(websocket, {
        "type": "registered",
        "subscriber": number,
        "bts_id": BTS_ID,
        "timestamp_ms": now_ms(),
    })

    for record in sms_log:
        if record.get("to") == number and record.get("status") == "QUEUED":
            record["status"] = "DELIVERED"

            await send_to(number, {
                "type": "sms.incoming",
                **record,
            })

            sender = str(record.get("from", ""))

            if sender in subscribers:
                await send_to(sender, {
                    "type": "sms.status",
                    **record,
                })

    await broadcast_status()

    try:
        while True:
            raw = await websocket.receive_text()

            try:
                message = json.loads(raw)
            except json.JSONDecodeError:
                await send_json(websocket, {
                    "type": "error",
                    "reason": "INVALID_JSON",
                    "timestamp_ms": now_ms(),
                })
                continue

            await handle_message(number, message)

    except WebSocketDisconnect:
        pass
    finally:
        current = subscribers.get(number)

        if current is not None and current.socket is websocket:
            subscribers.pop(number, None)

        for call in list(calls.values()):
            if (
                call.get("state") in {"RINGING", "CONNECTED"}
                and number in {call.get("caller"), call.get("callee")}
            ):
                call["state"] = "ENDED"
                call["ended_ms"] = now_ms()

                other = (
                    call["callee"]
                    if call["caller"] == number
                    else call["caller"]
                )

                await send_to(other, {
                    "type": "call.ended",
                    "reason": "PEER_DISCONNECTED",
                    "call": call_view(call),
                })

        await broadcast_status()


async def hardware_reader_loop():
    port = None
    previous_latency = None
    jitter_ewma = 0.0

    # BTS RFC2217 PERSISTENT RX BUFFER V1
    # RFC2217/Wokwi can time out in the middle of a long JSON record.
    # Preserve partial bytes until a newline arrives instead of attempting
    # json.loads() on an incomplete line and unnecessarily reconnecting.
    rx_buffer = bytearray()

    while True:
        try:
            if port is None:
                port = await asyncio.to_thread(
                    serial.serial_for_url,
                    BTS_HARDWARE_URL,
                    baudrate=115200,
                    timeout=0.5,
                )
                await asyncio.sleep(0.25)
                await asyncio.to_thread(port.reset_input_buffer)
                rx_buffer.clear()
                network["hardware_error"] = None

            raw = await asyncio.to_thread(port.readline)

            if not raw:
                await asyncio.sleep(0)
                continue

            rx_buffer.extend(raw)

            if b"\n" not in rx_buffer:
                if len(rx_buffer) > 16384:
                    network["hardware_error"] = (
                        "BTS hardware serial record exceeded 16384 bytes "
                        "without a newline"
                    )
                    rx_buffer.clear()

                await asyncio.sleep(0)
                continue

            while b"\n" in rx_buffer:
                raw_line, _, remainder = rx_buffer.partition(b"\n")
                rx_buffer = bytearray(remainder)

                line = raw_line.rstrip(b"\r").decode(
                    "utf-8",
                    errors="replace",
                ).strip()

                if not line.startswith(BTS_HARDWARE_PREFIX):
                    continue

                payload = json.loads(line[len(BTS_HARDWARE_PREFIX):])

                if payload.get("schema") != BTS_HARDWARE_SCHEMA:
                    continue

                if payload.get("bts_id") != BTS_ID:
                    continue

                latency = float(payload["latency_ms"])

                if previous_latency is not None:
                    jitter_sample = abs(latency - previous_latency)
                    jitter_ewma = (
                        (0.25 * jitter_sample) +
                        (0.75 * jitter_ewma)
                    )

                previous_latency = latency

                network.update({
                    "traffic_load_pct": float(payload["traffic_load_pct"]),
                    "latency_ms": latency,
                    "packet_loss_pct": float(payload["packet_loss_pct"]),
                    "jitter_ms": jitter_ewma,
                    "rssi_dbm": float(payload["rssi_dbm"]),
                    "radio_temperature_c": float(payload["pa_temp_c"]),
                    "radio_operational": bool(payload["radio_operational"]),
                    "backhaul_available": bool(payload["backhaul_available"]),
                    "link_up": bool(payload["link_up"]),
                    "upstream_reachable": bool(payload["upstream_reachable"]),
                    "cabinet_temp_c": float(payload["cabinet_temp_c"]),
                    "humidity_pct": float(payload["humidity_pct"]),
                    "rf_forward_w": float(payload["rf_forward_w"]),
                    "rf_reflected_w": float(payload["rf_reflected_w"]),
                    "reflection_ratio": float(payload["reflection_ratio"]),
                    "fan_operational": bool(payload["fan_operational"]),
                    "interference": bool(payload["interference"]),
                    "radio_fault": bool(payload["radio_fault"]),
                    "alarm": bool(payload["alarm"]),
                    "hardware_last_seen_ms": now_ms(),
                    "hardware_timestamp_ms": int(payload["timestamp_ms"]),
                    "hardware_error": None,
                })

        except asyncio.CancelledError:
            if port is not None:
                try:
                    port.close()
                except Exception:
                    pass
            raise

        except Exception as exc:
            network["hardware_error"] = str(exc)

            if port is not None:
                try:
                    port.close()
                except Exception:
                    pass
                port = None

            rx_buffer.clear()
            await asyncio.sleep(1.0)


@app.on_event("startup")
async def startup_tasks():
    async def live_metrics_loop():
        while True:
            for call in list(calls.values()):
                if call.get("state") != "CONNECTED":
                    continue

                metric_payload = {
                    "type": "call.metrics",
                    "call": call_view(call),
                }

                await send_to(call["caller"], metric_payload)
                await send_to(call["callee"], metric_payload)

            await asyncio.sleep(1.0)

    asyncio.create_task(hardware_reader_loop())
    asyncio.create_task(live_metrics_loop())
