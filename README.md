# Tikbalang

Flood-response logistics that keeps deciding after the network dies.

A sensor node sits on the bank and classifies the hazard **on the chip**, with no
cloud round trip. It looks down at the water rather than reaching into it: a
contact probe fails by corroding, and a corroded electrode reports DRY while the
road is under water, which is the worst failure this system can have. A
rangefinder can only fail by going silent, and silence is detectable. A dispatch layer turns that verdict into a route that avoids the
crossings a ground unit cannot pass. The payload is a power bank, because power is
what brings communication back, and a charged phone in a dead-tower zone is useless
without a radio to talk to.

In a flood the towers go down first. Every delivery system we could find phones home
to decide what to do next. Ours does not, and the claim is checkable rather than
asserted: every telemetry frame carries `decided_on`, which reads `device` or
`cloud` and is never hardcoded. Cut the uplink and watch it stay on `device` while
the central dispatch panel returns a real error from a real failed call.

**1.81 billion people are directly exposed to flood depths over 0.15 m in a
1-in-100-year event, and 89% of them live in low- and middle-income countries**
(Rentschler, Salhab & Jafino, *Flood Exposure and Poverty in 188 Countries*, Nature
Communications, 2022). Our classifier calls anything over 30 mm impassable for a
ground unit. That is the same regime the exposure literature measures, and it is why
the sensing has to be cheap enough to leave in the water.

Built at HackMIT 2026.

---

## What runs today

Stated precisely, because a README that overclaims is worse than one that does not.

| Layer | What exists | State |
|---|---|---|
| **Field node** | `node/classify.cpp` four-state hazard classifier, `node/frame.cpp` telemetry, `firmware/uno_q/` running on an Arduino UNO Q | **Flashed and streaming real readings.** The STM32U585 runs `classify.cpp` unchanged, reading water depth from a downward-looking rangefinder and disturbance from an IMU. The sketch symlinks the same sources the simulator compiles, so board and simulator cannot drift. |
| **Surface unit** | `firmware/boat/` reading an 8x8 matrix ToF, reusing `nav/avoid.cpp` unchanged; `ops/link_boat.py` carrying the node's verdict to it | Compiles and boot-tested on hardware, including the no-sensor fault path. Needs a second board to run with its sensor attached. |
| **Transport** | `ops/ws_bridge.py`, zero dependencies: WebSocket for consoles, TCP line ingest for producers, REST + OpenAPI | Runs. Survives malformed input, wrong baud rates, dead clients and client churn. |
| **Dispatch** | `dispatch/triage.py` calling a real cloud model, with the node's own verdict as the fallback | Runs. The failure path is a real network failure, not a simulated one. |
| **Console** | `dashboard/index.html`, single file, no framework | Runs. Central panel is driven by the real triage result, not by a button. |
| **Navigation** | `nav/` geo, dead reckoning, mission state machine, obstacle avoidance | The reactive half (`avoid.cpp`) runs on the surface unit unchanged. The waypoint half runs only in `sim/`, because a hull indoors has no GNSS and no wheel odometry, so there is no position to follow waypoints with. |

What is **not** done: `MOUNT_HEIGHT_MM` in `firmware/uno_q/uno_q.ino` is still a
placeholder, so reported depths carry a fixed offset until the sensor is measured
in its final mount. The surface unit has never run with its lidar attached, for
want of a second board. `firmware/esp32/` and `planning/` are empty.

**The unit is not autonomous and the README will not say it is.** There is no
position estimate on the water, so `pose` is `null` in every real unit frame and
the console draws no marker. The unit reacts to obstacles and to the node's
verdict. The decision is autonomous; the locomotion is commanded.

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
        +---> firmware/boat    real hardware, 20 Hz control loop
                               (avoid only: no GNSS on water, so no waypoints)
        +---> sim/             laptop build, fake sensors, same code
                                    |
                                    v
                            dashboard/  live telemetry over WebSocket
```

`nav/` compiles unchanged for both targets. That is deliberate. Every line of
navigation logic is testable on a laptop with no hardware attached, which is why
the dashboard and the nav loop were both working before the hull existed.

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

Read it carefully, because it is easy to misread. With the 1 Hz correction on,
`err_m` is dominated by the noise of the last GPS fix, not by dead-reckoning
drift. The simulator hands the estimator the rover's own `m_per_tick`, so
calibration error is exactly zero and drift alone is 0.02 m across the whole
290 s run. Commenting out `correct_with_gps` makes that number go **down**, not
up.

The correction earns its place the moment calibration is imperfect, which is the
only case that exists on real hardware. Measured against the same mission, miss
distance from the final waypoint:

| `m_per_tick` error | 1 Hz GPS on | GPS off |
|---|---|---|
| 0% (the simulator's ideal) | 2.94 m | 1.99 m |
| 2% | 3.53 m | 3.19 m |
| 5% | 3.83 m | 5.01 m |
| 10% | 3.37 m | 7.89 m |

That is the real argument: the encoders carry the rover between fixes, and the
fixes stop an uncalibrated tick constant from compounding without bound.

## Repository layout

| Path | What it is |
|---|---|
| `nav/` | Navigation core. Pure C++, no platform headers. Builds for laptop and MCU. |
| `sim/` | Synthetic rover and native entry point. Develop without hardware. |
| `firmware/uno_q/` | Field node. Flashed, streaming real readings. |
| `firmware/boat/` | Surface unit. Matrix ToF, servo fins, reactive avoidance. |
| `firmware/esp32/` | Empty. Kept as a marker, not a claim. |
| `dashboard/` | Live telemetry view. |
| `dispatch/` | Map to coordinates, voice dispatch. |
| `planning/` | Flood risk route scoring over public datasets. |
| `ops/` | Telemetry ingest and log sync. |
| `docs/` | Notes, calibration records, demo script. |

## Calibration

**`MOUNT_HEIGHT_MM`** is the one that decides whether the demo is telling the
truth. It is the distance in mm from the node's sensor face to the dry bottom of
the tray, and depth is `MOUNT_HEIGHT_MM - measured`. Get it wrong and every depth
is wrong by the same offset, which is the easiest bug in the build to miss
because the numbers still look plausible. Measure it once, with the sensor
clamped in the position it will demo in, by sending `cal` on the node's serial
port and reading back the raw range.

Two more live in `docs/calibration.md` and matter only if the waypoint follower
is ever used on a wheeled vehicle:

- **`m_per_tick`** drive a measured 2 m in a straight line, count encoder ticks,
  divide.
- **IMU heading offset** point the vehicle at a known bearing, record the delta.

## Safety

Every failure resolves to stop. A sensor that returns no range sets
`obstacle_mm` to `null`, which is read as a fault and never as open water, so
`go` will not start the fins while the lidar is silent. `ops/link_boat.py`
commands `hold` on anything that is not a confident PASSABLE: an IMPASSABLE
verdict, an UNKNOWN one, a node that has gone stale, an unreachable bridge. It
also writes `hold` on its own exit, because a link process that dies while the
fins are still moving is a unit nobody is commanding.

## Sponsor challenges entered

- **Arduino / Touch Grass** physical world sensing drives the hazard classifier, on an UNO Q
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
