# Modeled Wokwi Inputs

This upgrade replaces direct potentiometer/switch injection for network, radio and traffic metrics with three causal simulation components.

## Backhaul Network Model
Controls: base latency, injected congestion, traffic-to-congestion coupling, link impairment, jitter, physical link failure and upstream failure.
Outputs: latency, packet loss, physical-link state and upstream reachability.
The Baseband Traffic Generator also feeds this model so increasing traffic can automatically increase backhaul stress.

## Radio and Antenna Link Model
Controls: transmit power, path loss, interference, reflected-power ratio/antenna mismatch, fading depth and radio hardware fault.
Outputs: Received Signal Strength Indicator, forward Radio Frequency power, reflected Radio Frequency power and radio-operational state.
The ESP32 continues to calculate Voltage Standing Wave Ratio and return loss from forward/reflected power.

## Baseband Traffic Generator
Controls: active users, average user demand, nominal cell capacity and traffic burstiness.
Output: traffic load percentage.

## What remains unchanged
The ESP32 still converts the model outputs into the same engineering variables, signal-processing features and 33 Artificial Intelligence inputs. The laptop temporal Artificial Intelligence, Raspberry Pi Pico classifier/decision layer and ESP32 deterministic safety guardrails therefore remain compatible.
