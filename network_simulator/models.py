from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from time import monotonic
from typing import Dict, Optional


class CallState(str, Enum):
    RINGING = "RINGING"
    CONNECTED = "CONNECTED"
    ENDED = "ENDED"
    REJECTED = "REJECTED"
    FAILED = "FAILED"


@dataclass
class FaultState:
    congestion: bool = False
    impairment: bool = False
    link_failure: bool = False
    upstream_failure: bool = False
    path_loss: bool = False
    interference: bool = False
    antenna_mismatch: bool = False
    radio_failure: bool = False

    def as_dict(self) -> Dict[str, bool]:
        return {
            "congestion": self.congestion,
            "impairment": self.impairment,
            "link_failure": self.link_failure,
            "upstream_failure": self.upstream_failure,
            "path_loss": self.path_loss,
            "interference": self.interference,
            "antenna_mismatch": self.antenna_mismatch,
            "radio_failure": self.radio_failure,
        }


@dataclass
class CallMetrics:
    packets_sent: int = 0
    packets_received: int = 0
    packets_dropped: int = 0
    latency_ms: float = 45.0
    jitter_ms: float = 4.0
    packet_loss_pct: float = 0.0
    rssi_dbm: float = -61.0
    traffic_load_pct: float = 0.0
    quality: str = "GOOD"

    def as_dict(self) -> dict:
        return {
            "packets_sent": self.packets_sent,
            "packets_received": self.packets_received,
            "packets_dropped": self.packets_dropped,
            "latency_ms": round(self.latency_ms, 1),
            "jitter_ms": round(self.jitter_ms, 1),
            "packet_loss_pct": round(self.packet_loss_pct, 2),
            "rssi_dbm": round(self.rssi_dbm, 1),
            "traffic_load_pct": round(self.traffic_load_pct, 1),
            "quality": self.quality,
        }


@dataclass
class CallSession:
    call_id: str
    caller: str
    callee: str
    state: CallState = CallState.RINGING
    created_at_monotonic: float = field(default_factory=monotonic)
    connected_at_monotonic: Optional[float] = None
    ended_at_monotonic: Optional[float] = None
    metrics: CallMetrics = field(default_factory=CallMetrics)

    def involves(self, number: str) -> bool:
        return number in (self.caller, self.callee)

    def peer_of(self, number: str) -> str:
        if number == self.caller:
            return self.callee
        if number == self.callee:
            return self.caller
        raise ValueError(f"{number} is not part of call {self.call_id}")

    def duration_s(self) -> int:
        if self.connected_at_monotonic is None:
            return 0
        end = self.ended_at_monotonic or monotonic()
        return max(0, int(end - self.connected_at_monotonic))

    def as_dict(self) -> dict:
        return {
            "call_id": self.call_id,
            "caller": self.caller,
            "callee": self.callee,
            "state": self.state.value,
            "duration_s": self.duration_s(),
            "metrics": self.metrics.as_dict(),
        }
