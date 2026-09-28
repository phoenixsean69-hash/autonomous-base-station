# ABS Mobile Network Simulator

This is the call/network layer for the Autonomous Base Station mobile demo.

## Simulated subscribers

- Subscriber A: `0712 000 001`
- Subscriber B: `0712 000 002`

## Start the server

```powershell
cd network_simulator
.\run.ps1
```

The server listens on:

- HTTP: `http://0.0.0.0:8000`
- WebSocket: `ws://<laptop-ip>:8000/ws/<subscriber-number>`

## Test the complete two-user call flow

Keep the server running in terminal 1.

In terminal 2:

```powershell
cd network_simulator
.\test.ps1
```

The test:

1. connects Subscriber A
2. connects Subscriber B
3. A calls B
4. B receives the call
5. B answers
6. the server emits one-second network metrics
7. A ends the call

## WebSocket events

Client -> server:

- `call.start`
- `call.answer`
- `call.reject`
- `call.end`
- `ping`

Server -> client:

- `registered`
- `subscribers.status`
- `call.ringing`
- `call.incoming`
- `call.connected`
- `call.metrics`
- `call.rejected`
- `call.ended`
- `call.failed`
- `faults.updated`

## Fault injection API

Read faults:

```powershell
Invoke-RestMethod http://127.0.0.1:8000/faults
```

Inject packet impairment:

```powershell
Invoke-RestMethod `
  -Method Patch `
  -Uri http://127.0.0.1:8000/faults `
  -ContentType "application/json" `
  -Body '{"impairment":true}'
```

Inject congestion:

```powershell
Invoke-RestMethod `
  -Method Patch `
  -Uri http://127.0.0.1:8000/faults `
  -ContentType "application/json" `
  -Body '{"congestion":true}'
```

Drop the link:

```powershell
Invoke-RestMethod `
  -Method Patch `
  -Uri http://127.0.0.1:8000/faults `
  -ContentType "application/json" `
  -Body '{"link_failure":true}'
```

Reset all faults:

```powershell
Invoke-RestMethod `
  -Method Post `
  -Uri http://127.0.0.1:8000/faults/reset
```

## Current model

A connected call generates:

- 50 packets/s each direction
- 100 packet events/s total
- latency
- jitter
- packet-loss percentage
- RSSI
- traffic load
- quality state

The mobile app connection is the next integration step.
