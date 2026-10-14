# BTS-001 Hardware Circuit

This Wokwi/ESP32 circuit represents the standalone BTS hardware plant.

It includes:

- ESP32 BTS controller
- cabinet DHT22 temperature/humidity sensor
- DS18B20 power-amplifier / radio temperature sensor
- RSSI knob
- traffic/channel-load knob
- RF forward-power knob
- RF reflected-power knob
- latency knob
- packet-loss knob
- RADIO ENABLE switch
- BACKHAUL switch
- COOLING FAN switch
- INTERFERENCE fault switch
- RADIO FAULT switch
- RADIO/BACKHAUL/ALARM/TX LEDs
- 20x4 status LCD

## Build

```powershell
cd "$env:USERPROFILE\Desktop\BTS\hardware"
.\build.ps1
```

Then open the `hardware` folder in VS Code and run:

```text
F1 -> Wokwi: Start Simulator
```

RFC2217 endpoint:

```text
rfc2217://localhost:4010
```

Telemetry prefix:

```text
BTS_HW|
```

Telemetry schema:

```text
bts.hw.v1
```

The next step is to connect the Python BTS service on port 8100 to this
hardware endpoint so live calls/SMS and the physical BTS plant become one
system.
