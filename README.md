<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/tanod-logo-dark.svg">
    <img alt="Tanod" src="docs/assets/tanod-logo.svg" width="440">
  </picture>
</p>

<p align="center"><em>A flood crossing sensor that decides on the chip and shows the answer with no network at all.</em></p>

---

A sensor node sits on the bank and classifies the hazard **on the chip**, with no
cloud round trip. It looks down at the water rather than reaching into it: a
contact probe fails by corroding, and a corroded electrode reports DRY while the
road is under water, which is the worst failure this system can have. A
rangefinder can only fail by going silent, and silence is detectable.

Then it delivers the answer where the network is not. The node blinks its verdict
on a two second cycle, so a person standing at the crossing can read it with the
uplink down, the laptop gone and the console closed.

In a flood the towers go down first, and the things that answer "is this crossing
passable right now" all phone home to do it. Ours does not, and the claim is
checkable rather than asserted: the node's telemetry frames carry `decided_on`,
which reads `device` or `cloud` and is computed rather than hardcoded. Cut the
uplink and watch it stay on `device` while the central dispatch panel returns a
real error from a real failed call.

**The decision is autonomous. The locomotion is commanded.** There is no position
estimate anywhere in the hardware path, so `pose` is null in every real unit frame.
The classifier is threshold logic, not machine learning. The surface unit floats
but has never run with its own depth sensor. All of that is stated again, in more
detail, under [What runs today](#what-runs-today).

**1.81 billion people are directly exposed to flood depths over 0.15 m in a
1-in-100-year event, and 89% of them live in low- and middle-income countries**
(Rentschler, Salhab & Jafino, *Flood Exposure and Poverty in 188 Countries*, Nature
Communications, 2022). Our classifier calls anything over 30 mm impassable for a
ground unit. That is the same regime the exposure literature measures, and it is why
the sensing has to be cheap enough to leave in the water.

Built at HackMIT 2026.

---

## The name

**Tanod** is Tagalog for the barangay watchman: the village-level guard, posted
where the national police cannot immediately reach, described in the Local
Government Code framework as a front liner "in the preparation and response to
any type of atrocities, public disorders, emergencies and even disasters."

That is not a metaphor for the architecture. It is the architecture. The node
stands at the crossing and decides for itself when nothing upstream can reach
it.

This project was previously named after the tikbalang, the Philippine trickster
that leads travellers astray on the road. For a system whose only job is
telling you whether you can safely cross, that was exactly backwards.

---

## What runs today

Stated precisely, because a README that overclaims is worse than one that does not.

| Layer | What exists | State |
|---|---|---|
| **Field node** | `node/classify.cpp` four-state hazard classifier, `node/frame.cpp` telemetry, `firmware/uno_q/` running on an Arduino UNO Q, blinking its verdict locally | **Flashed and streaming real readings.** The STM32U585 runs `classify.cpp` unchanged, reading water depth from a downward-looking rangefinder and disturbance from an IMU. The sketch symlinks the same sources the simulator compiles, so board and simulator cannot drift. |
| **Surface unit** | `firmware/boat/` reading an 8x8 matrix ToF; `nav/gap.cpp` choosing a direction from the field; `nav/avoid.cpp` unchanged when there is none; `ops/link_boat.py` carrying the node's verdict to it | Compiles in all three drive configurations and boot-tested on hardware, including the no-sensor fault path. Needs a second board to run with its sensor attached. |
| **Steering** | `nav/gap.cpp`, follow-the-gap over the 8 columns of the ToF array | Runs. 13 native cases in `sim/gap_test.cpp` pass, including that an unknown column is never steered into. No map, no waypoint, no position estimate. |
| **Tests** | `ops/stress.py`, `ops/preflight.sh` | 19 adversarial cases pass with no hardware attached. Preflight gates the demo. |
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

Tanod carries its own navigation, its own obstacle handling and its own
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

## Running it

Three processes. They do **not** all have to be on one laptop, and on demo day
they are not: whoever is holding a board runs a forwarder, and whoever is
driving the console runs the bridge. That split is the entire reason
`ops/serial_forward.py` exists.

### Machine A, the bridge and the console

```bash
export OPENAI_API_KEY=...            # must be in THIS shell
python3 ops/ws_bridge.py --serve-only
```

Then open `http://127.0.0.1:8767/?src=live`.

Always use that URL with `?src=live`. The source is remembered in localStorage
and a stray keypress switches it to canned REPLAY, which must never be on
screen without someone saying the word "recorded" out loud first.

Serving the console **from the bridge** is not optional. `POST /triage` is
same-origin, so opening `dashboard/index.html` off the filesystem silently
breaks the cloud panel.

Find this machine's address once and tell everyone:

```bash
ipconfig getifaddr en0
```

### Machine B, whichever laptop holds the node

```bash
python3 ops/serial_forward.py --port /dev/cu.usbmodemXXXX --host <machine A>
```

### Machine C, if the surface unit has its own board

```bash
python3 ops/link_boat.py      --port /dev/cu.usbmodemBOAT               # first
python3 ops/serial_forward.py --port /dev/cu.usbmodemBOAT --host <machine A>
```

`link_boat.py` opens the port **before** the forwarder on purpose. Opening a
USB CDC port toggles DTR and resets the board, and you want that reset at
startup rather than in the middle of a demo.

### With no hardware at all

The software path is the backup demo, so it has to stand alone:

```bash
python3 ops/serial_forward.py --fake --host 127.0.0.1
```

### Check it, do not assume it

```bash
sh ops/preflight.sh
```

It checks the bridge, whether the node has ever been seen, whether its frame is
actually **fresh** (a cached frame from a dead sensor is byte-identical to a
live one, so age is the only tell), whether the depth is saturated, whether
CUT NETWORK was left latched from a previous run, whether the cloud path really
answers, and whether the spec is served. Every failure prints the command that
fixes it. Exit code is the failure count.

### Known ways this breaks on the day

- **The bridge IP moves.** Campus DHCP leases are an hour. Re-check before the demo.
- **The macOS firewall prompts** on the first inbound connection to a new python
  binary, and a teammate's first connection silently fails while the dialog waits.
- **Campus wifi may isolate clients**, which blocks laptop-to-laptop entirely.
  Prove the path with `--fake` before relying on it.
- **A background browser tab freezes the console.** `requestAnimationFrame` is
  suspended when the tab is hidden; it resumes on focus. This is not a bug and
  it is not worth debugging at 9am.

## Tests

```bash
python3 ops/stress.py      # 19 cases, whole system, no hardware attached
make gaptest               # 13 cases, the steering algorithm, natively
make && ./build/sim        # the navigation simulator
```

`ops/stress.py` gives every case its own bridge. Cases that hammer the ingest
port leave a bridge busy draining for seconds afterwards, and a shared bridge
turns that into phantom failures in whatever runs next. A suite that reports
failures it cannot reproduce in isolation is worse than no suite.

Measured on this hardware: sustained ingest of about **70,000 frames per
second** with a console attached, roughly 14,000 times the 5 Hz demo rate.

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

Two, because two have code. The earlier list named eight, five of which existed
only as a line in a planning document. A repository that claims a track it did
not build is a repository a judge can disprove in ten seconds, and every other
claim on this page gets read differently afterwards.

- **Arduino / Touch Grass** — `firmware/uno_q/`, flashed to an UNO Q and
  streaming real depth and disturbance readings; `firmware/boat/`, compiled and
  boot-tested on the same silicon.
- **OpenAI** — `dispatch/triage.py`, a real HTTPS call whose failure is a real
  network failure, with the node's own verdict as the fallback.

Considered and not entered: Deepgram, ElevenLabs, Elastic, Dropbox, Voloridge.
Each is one line in `docs/PLAN.md` and nothing more.

## Team

Kevin Chen ([@KevinC-1000](https://github.com/KevinC-1000)),
Anjelo Go ([@Anjelogo](https://github.com/Anjelogo)),
Aiaru Naukenova ([@aiarunkn](https://github.com/aiarunkn)),
Arsenii Chan ([@ArseniiChan](https://github.com/ArseniiChan))
