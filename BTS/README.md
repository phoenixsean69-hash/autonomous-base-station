# BTS-001 — Basic BTS Simulator

This is the first standalone BTS for the Autonomous Base Station project.

## Current flow

```text
Mobile App
    |
    | WebSocket
    v
BTS-001
Desktop\BTS
    |
    | Backhaul - next step
    v
Main Autonomous Base Station
```

## Current features

- Subscriber registration
- Phone-to-phone call signalling
- SMS delivery
- Active-call tracking
- Basic BTS telemetry
- Traffic load
- Latency
- Packet loss
- Jitter
- RSSI
- Radio temperature
- Radio operational state
- Backhaul operational state

## Setup

```powershell
cd "$env:USERPROFILE\Desktop\BTS"
.\setup.ps1
```

## Start

```powershell
cd "$env:USERPROFILE\Desktop\BTS"
.\run.ps1
```

BTS port: `8100`

HTTP:

```text
http://127.0.0.1:8100/health
http://127.0.0.1:8100/state
http://127.0.0.1:8100/telemetry
```

Subscriber WebSockets:

```text
ws://LAPTOP-IP:8100/ws/0712000001
ws://LAPTOP-IP:8100/ws/0712000002
```

Call start:

```json
{"type":"call.start","to":"0712000002"}
```

SMS:

```json
{"type":"sms.send","to":"0712000002","body":"Hello from BTS-001"}
```

## Next milestone

Connect BTS-001 to the existing Main Autonomous Base Station through a
bidirectional backhaul. After that we will add controlled fault injection and
closed-loop recovery.
