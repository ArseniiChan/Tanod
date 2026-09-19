# Tikbalang

Autonomous ground rover that delivers power banks to people cut off by floodwater.

Dispatched by dropping a pin on a map. It navigates there on its own and releases
its payload on arrival. **The entire autonomy stack runs onboard an Espressif
ESP32-S3, offline.** Cut the network mid-run and it keeps going. That is the point:
in a flood, the network is the first thing you lose.

Built at HackMIT 2026.

---

## Why offline matters

Disaster response tooling assumes connectivity that disaster removes. Typhoon
flooding in the Philippines routinely takes cell towers down across whole
municipalities. A delivery robot that phones home for every decision is a robot
that stops at the moment it becomes useful.

Tikbalang carries its own navigation, its own obstacle handling and its own
hazard classification. The laptop is a viewer, never a controller.

## Architecture

```
GPS + IMU + wheel encoders
        |
        v
   nav/  (plain C++, zero Arduino dependencies)
   geo         haversine distance, initial bearing, projection
   deadreckon  complementary filter, encoder odometry + gyro heading
   mission     waypoint state machine, arrival, sequencing
   avoid       reactive obstacle behaviour
        |
        +---> firmware/esp32   real hardware, 20 Hz control loop
        +---> sim/             laptop build, fake sensors, same code
                                    |
                                    v
                            dashboard/  live telemetry over WebSocket
```

`nav/` compiles unchanged for both targets. That is deliberate. Every line of
navigation logic is testable on a laptop with no hardware attached, which is why
the dashboard and the nav loop were both working before the rover existed.

## Build and run the simulator

No hardware required.

```bash
make
./build/sim
```

The simulator drives a synthetic rover through a two waypoint mission with an
obstacle parked on the first leg, and emits the same JSON telemetry frame the
firmware emits, at the same rate.

```json
{"t":980,"lat":42.359936,"lon":-71.091964,"fix":1,"hdg":45.1,"spd":-0.23,
 "encL":9667,"encR":9667,"d":[494],"wp":0,"mode":"AVOIDING","err_m":2.36}
```

`err_m` is the live gap between the dead reckoned estimate and ground truth.
Comment out the `correct_with_gps` call in `sim/main_native.cpp` to watch that
number grow without bound, which is the argument for the encoders in one line.

## Repository layout

| Path | What it is |
|---|---|
| `nav/` | Navigation core. Pure C++, no platform headers. Builds for laptop and ESP32. |
| `sim/` | Synthetic rover and native entry point. Develop without hardware. |
| `firmware/esp32/` | Sensor drivers, motor PWM, encoder ISR, WiFi, telemetry server. |
| `dashboard/` | Live telemetry view. |
| `dispatch/` | Map to coordinates, voice dispatch. |
| `planning/` | Flood risk route scoring over public datasets. |
| `ops/` | Telemetry ingest and log sync. |
| `docs/` | Notes, calibration records, demo script. |

## Calibration

Two numbers decide whether a waypoint run is clean or ends in a wall. Both take
about fifteen minutes and both live in `docs/calibration.md` once measured.

- **`m_per_tick`** drive a measured 2 m in a straight line, count encoder ticks,
  divide.
- **IMU heading offset** point the rover at a known bearing, record the delta.

## Safety

The control loop carries a motor watchdog. If the nav loop misses its deadline,
both motors stop. This is the difference between a software fault being a pause
and a software fault being a rover off the edge of the judging table.

## Sponsor challenges entered

- **Espressif** full autonomy onboard an ESP32-S3, offline, no companion computer
- **Arduino / Touch Grass** physical world sensing drives the hazard classifier
- **Voloridge** flood risk route scoring over large public datasets
- **Deepgram** speech to text for voice dispatch
- **ElevenLabs** spoken status and confirmation
- **Elastic** telemetry indexing and search
- **OpenAI** dispatch triage
- **Dropbox** mission log sync

## Team

Kevin Chen ([@KevinC-1000](https://github.com/KevinC-1000)),
Anjelo Go ([@Anjelogo](https://github.com/Anjelogo)),
Aiaru Naukenova ([@aiarunkn](https://github.com/aiarunkn)),
Arsenii Chan ([@ArseniiChan](https://github.com/ArseniiChan))
