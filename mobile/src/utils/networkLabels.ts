import type {
  NetworkMetrics,
} from "../context/DialerContext";

export function signalLabel(
  rssiDbm?: number,
) {
  if (
    rssiDbm === undefined
  ) {
    return "Ready";
  }

  if (rssiDbm >= -70) {
    return "Strong";
  }

  if (rssiDbm >= -85) {
    return "Fair";
  }

  if (rssiDbm >= -100) {
    return "Weak";
  }

  return "Very weak";
}

export function qualityLabel(
  quality?: string,
) {
  switch (
    quality?.toUpperCase()
  ) {
    case "GOOD":
      return "Good";
    case "DEGRADED":
      return "Fair";
    case "POOR":
      return "Poor";
    case "FAILED":
      return "Unavailable";
    default:
      return "Ready";
  }
}

export function delayLabel(
  latencyMs?: number,
) {
  if (
    latencyMs === undefined
  ) {
    return "Ready";
  }

  if (latencyMs < 100) {
    return "Low";
  }

  if (latencyMs < 250) {
    return "Noticeable";
  }

  return "High";
}

export function interruptionLabel(
  metrics?: NetworkMetrics | null,
) {
  if (!metrics) {
    return "Ready";
  }

  if (
    metrics.packet_loss_pct < 1 &&
    metrics.jitter_ms < 15
  ) {
    return "None";
  }

  if (
    metrics.packet_loss_pct < 5 &&
    metrics.jitter_ms < 35
  ) {
    return "Occasional";
  }

  return "Frequent";
}
