from __future__ import annotations

import random
from dataclasses import dataclass

from models import CallMetrics, FaultState


@dataclass
class PacketStep:
    sent: int
    received: int
    dropped: int


class NetworkEngine:
    """
    Lightweight voice-call network simulator.

    One connected call represents a bidirectional compressed-voice media
    stream. The simulator generates 50 packets/second per direction
    (20 ms packetization), so a call produces 100 packet events per second.
    """

    PACKETS_PER_SECOND_PER_DIRECTION = 50
    TOTAL_PACKETS_PER_SECOND = 100

    def __init__(self, seed: int = 20260929):
        self._rng = random.Random(seed)

    def _loss_probability(
        self,
        faults: FaultState,
        active_calls: int,
    ) -> float:
        if faults.link_failure or faults.upstream_failure:
            return 1.0

        probability = 0.005

        if faults.congestion:
            probability += 0.06

        if faults.impairment:
            probability += 0.20

        if faults.interference:
            probability += 0.05

        if faults.path_loss:
            probability += 0.03

        if faults.radio_failure:
            probability = 1.0

        # Additional traffic raises queue pressure.
        probability += max(0, active_calls - 1) * 0.015

        return min(max(probability, 0.0), 1.0)

    def _latency_ms(
        self,
        faults: FaultState,
        active_calls: int,
    ) -> float:
        if faults.link_failure or faults.upstream_failure:
            return 1000.0

        value = 42.0

        if faults.congestion:
            value += 190.0

        if faults.impairment:
            value += 80.0

        if faults.interference:
            value += 28.0

        if faults.path_loss:
            value += 18.0

        value += max(0, active_calls - 1) * 25.0
        value += self._rng.uniform(-7.0, 7.0)

        return max(10.0, min(value, 1000.0))

    def _jitter_ms(
        self,
        faults: FaultState,
        active_calls: int,
    ) -> float:
        if faults.link_failure or faults.upstream_failure:
            return 0.0

        value = 3.5

        if faults.congestion:
            value += 30.0

        if faults.impairment:
            value += 18.0

        if faults.interference:
            value += 10.0

        value += max(0, active_calls - 1) * 4.0
        value += self._rng.uniform(0.0, 4.0)

        return max(0.0, value)

    def _rssi_dbm(self, faults: FaultState) -> float:
        if faults.radio_failure:
            return -112.0

        value = -60.0 + self._rng.uniform(-1.5, 1.5)

        if faults.path_loss:
            value -= 24.0

        if faults.interference:
            value -= 11.0

        if faults.antenna_mismatch:
            value -= 3.0

        return max(-120.0, min(value, -45.0))

    @staticmethod
    def _quality(
        latency_ms: float,
        jitter_ms: float,
        packet_loss_pct: float,
        rssi_dbm: float,
    ) -> str:
        if (
            packet_loss_pct >= 30.0
            or latency_ms >= 600.0
            or rssi_dbm <= -105.0
        ):
            return "FAILED"

        if (
            packet_loss_pct >= 8.0
            or latency_ms >= 250.0
            or jitter_ms >= 40.0
            or rssi_dbm <= -90.0
        ):
            return "POOR"

        if (
            packet_loss_pct >= 2.0
            or latency_ms >= 150.0
            or jitter_ms >= 20.0
            or rssi_dbm <= -80.0
        ):
            return "DEGRADED"

        return "GOOD"


    def base_station_snapshot(
        self,
        faults: FaultState,
        active_calls: int,
    ) -> dict:
        active_calls = max(0, int(active_calls))

        access_rssi_dbm = self._rssi_dbm(faults)

        rf_forward_w = 80.0
        rf_reflected_w = 1.2
        radio_operational = True

        if faults.interference:
            rf_forward_w -= 4.0

        if faults.antenna_mismatch:
            rf_reflected_w = 18.0

        if faults.radio_failure:
            rf_forward_w = 3.0
            rf_reflected_w = 2.5
            radio_operational = False

        if faults.link_failure:
            backhaul_latency_ms = 1000.0
            backhaul_packet_loss_pct = 100.0
            backhaul_rssi_dbm = -120.0
        elif faults.upstream_failure:
            backhaul_latency_ms = 1000.0
            backhaul_packet_loss_pct = 100.0
            backhaul_rssi_dbm = -58.0 + self._rng.uniform(-1.0, 1.0)
        else:
            backhaul_latency_ms = 42.0

            if faults.congestion:
                backhaul_latency_ms += 190.0

            if faults.impairment:
                backhaul_latency_ms += 80.0

            backhaul_latency_ms += max(
                0,
                active_calls - 1,
            ) * 25.0

            backhaul_latency_ms += self._rng.uniform(
                -5.0,
                5.0,
            )

            backhaul_packet_loss_pct = 0.5

            if faults.congestion:
                backhaul_packet_loss_pct += 6.0

            if faults.impairment:
                backhaul_packet_loss_pct += 20.0

            backhaul_packet_loss_pct += max(
                0,
                active_calls - 1,
            ) * 1.5

            backhaul_packet_loss_pct = min(
                100.0,
                backhaul_packet_loss_pct,
            )

            backhaul_rssi_dbm = (
                -58.0 +
                self._rng.uniform(
                    -1.0,
                    1.0,
                )
            )

            if faults.impairment:
                backhaul_rssi_dbm -= 4.0

        traffic_load_pct = min(
            100.0,
            30.0 * active_calls,
        )

        return {
            "traffic_load_pct": round(
                traffic_load_pct,
                2,
            ),
            "access": {
                "signal_dbm": round(
                    access_rssi_dbm,
                    2,
                ),
                "rf_forward_w": round(
                    max(0.0, rf_forward_w),
                    2,
                ),
                "rf_reflected_w": round(
                    max(0.0, rf_reflected_w),
                    2,
                ),
                "radio_operational":
                    radio_operational,
            },
            "backhaul": {
                "latency_ms": round(
                    max(
                        10.0,
                        min(
                            1000.0,
                            backhaul_latency_ms,
                        ),
                    ),
                    2,
                ),
                "packet_loss_pct": round(
                    max(
                        0.0,
                        min(
                            100.0,
                            backhaul_packet_loss_pct,
                        ),
                    ),
                    2,
                ),
                "rssi_dbm": round(
                    max(
                        -120.0,
                        min(
                            -45.0,
                            backhaul_rssi_dbm,
                        ),
                    ),
                    2,
                ),
                "link_up":
                    not faults.link_failure,
                "upstream_reachable":
                    not faults.upstream_failure,
            },
        }

    def step(
        self,
        metrics: CallMetrics,
        faults: FaultState,
        active_calls: int,
    ) -> CallMetrics:
        sent = self.TOTAL_PACKETS_PER_SECOND
        loss_probability = self._loss_probability(
            faults,
            active_calls,
        )

        dropped = sum(
            1
            for _ in range(sent)
            if self._rng.random() < loss_probability
        )
        received = sent - dropped

        metrics.packets_sent += sent
        metrics.packets_received += received
        metrics.packets_dropped += dropped

        if metrics.packets_sent:
            metrics.packet_loss_pct = (
                metrics.packets_dropped
                / metrics.packets_sent
                * 100.0
            )

        metrics.latency_ms = self._latency_ms(
            faults,
            active_calls,
        )
        metrics.jitter_ms = self._jitter_ms(
            faults,
            active_calls,
        )
        metrics.rssi_dbm = self._rssi_dbm(faults)

        # A single voice call is treated as about 30% of the demonstration
        # base-station capacity; additional calls would raise this.
        metrics.traffic_load_pct = min(
            100.0,
            30.0 * max(active_calls, 1),
        )

        metrics.quality = self._quality(
            metrics.latency_ms,
            metrics.jitter_ms,
            metrics.packet_loss_pct,
            metrics.rssi_dbm,
        )

        return metrics
