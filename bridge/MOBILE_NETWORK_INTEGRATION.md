# Mobile Call Traffic to Base Station Integration

This stage connects the subscriber demonstration to the autonomous base-station controller.

```text
ABS Connect phones
        |
        v
network_simulator
        |
        | /base-station/telemetry
        v
bridge/live_ai_bridge.py
        |
        | ABS_NET_CMD
        v
ESP32 / Wokwi
        |
        +-- traffic load
        +-- backhaul delay
        +-- packet loss
        +-- backhaul signal
        +-- link / upstream state
        +-- RF forward/reflected power
        +-- radio operational state
        |
        v
33-feature temporal AI pipeline
```

The simulator does not replace power, environmental, thermal or vibration sensors.

If simulator updates stop for more than three seconds, the ESP32 automatically returns to the physical Wokwi circuit inputs.

The ESP32 remains the final deterministic safety authority.

Run the network simulator first, then the ESP32/Pico Wokwi simulations, then:

```powershell
py bridge\live_ai_bridge.py
```
