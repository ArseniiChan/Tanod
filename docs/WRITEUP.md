# Tikbalang — submission writeup

Paste-ready. Trim to whatever the form's character limit is, cutting from the
bottom. Everything in here is checkable against the repository or the screen.

---

## One line

A flood sensor that makes the call on its own chip, and keeps making it after
the network dies.

---

## What it does

Tikbalang classifies a flooded crossing as dry, passable, contaminated or
impassable, and it does that classification on a microcontroller with no cloud
round trip. A dispatch layer turns the verdict into a route that avoids
crossings a ground unit cannot pass. A surface unit reacts to obstacles and to
the node's verdict.

The claim we are making is narrow on purpose: **the decision layer keeps working
after the uplink is gone.** Every telemetry frame carries a field called
`decided_on`, which reads `device` or `cloud` and is never hardcoded. Cut the
uplink and watch it stay on `device` while the cloud panel returns a real error
from a real failed HTTPS request. The claim is checkable rather than asserted,
and that was the design goal before any of it was written.

---

## Why

In a flood the towers go down first, and every delivery and dispatch system we
could find phones home to decide what to do next.

1.81 billion people are directly exposed to flood depths over 0.15 m in a
1-in-100-year event, and 89% of them live in low- and middle-income countries
(Rentschler, Salhab & Jafino, *Flood Exposure and Poverty in 188 Countries*,
Nature Communications, 2022). Our classifier calls anything over 30 mm
impassable for a ground unit, which is the same regime that literature
measures, and it is why the sensing has to be cheap enough to leave in the
water and dumb enough to work without a network.

In November 2025, typhoons Tino and Uwan affected nearly nine million people in
the Philippines. The NDRRMC reported 259 dead and 2,311,340 individuals
displaced as of 13 November, and the President declared a state of national
calamity.

> **[Anjelo: this paragraph is yours to approve, rewrite in your own words, or
> cut entirely. Nobody should put it in without you.]** One of us is Filipino
> and has family in areas the floods reach. This is not a hypothetical problem
> chosen off a list.

---

## How it works

**Field node.** An Arduino UNO Q. `node/classify.cpp` runs on its STM32U585,
reading water depth from a downward-looking laser rangefinder and disturbance
from an IMU. The sketch does not contain a copy of the classifier; it symlinks
the same `classify.cpp` and `frame.cpp` the simulator compiles, so the board
and the simulator cannot drift apart about what counts as impassable.

We chose a non-contact rangefinder over a contact probe deliberately. A probe
in the water fails by corroding, and a corroded bottom electrode reports DRY
while the road is under water. That is a confident false negative and it is the
worst failure this system can produce. A rangefinder can only fail by going
silent, and silence is detectable: an unavailable sensor sets the hazard to
UNKNOWN, not to DRY, and the planner refuses to route a unit through
unsurveyed water.

**Transport.** `ops/ws_bridge.py`, Python standard library only, no
dependencies. WebSocket for consoles, TCP line ingest for producers, REST plus
an OpenAPI 3.1 document for everyone else. Each console gets a bounded queue and
its own sender thread, so one stalled browser cannot block the producer. We
found that the hard way: before the fix a slow client cost a healthy one 12,000
frames out of 20,000 and killed the producer. After it, the producer ran 2.6
million frames with no stall.

**Dispatch.** `dispatch/triage.py` makes a real HTTPS call to a real model, with
a short timeout, and falls back to the verdict the node already reached. The
failure path is a genuine network failure. Nothing simulates an outage.

**Surface unit.** `firmware/boat/` reads an 8x8 time-of-flight array for
obstacle range and reuses `nav/avoid.cpp` unchanged, the same file the simulator
runs. `ops/link_boat.py` polls the node's verdict off the bridge and writes
`hold` or `go` down the unit's serial port, so the node's offline decision has a
physical consequence.

**Console.** `dashboard/index.html`, one file, no framework, Canvas and
WebSocket.

---

## What is real and what is not

We would rather say this ourselves than have it found.

**Real, and running:** the node is flashed and streaming genuine readings at
5 Hz. The cloud call is real and its failure is real. `decided_on` has printed
both values in one session. The bridge and the console run. The unit firmware
compiles and was flashed to real silicon, where it boots, reports
`lidar not responding` with no sensor attached, and correctly refuses the `go`
command.

**Not real:** the surface unit has never run with its sensor attached, because
the node occupies our only board and the node has to stay fixed on the bank. A
depth sensor that floats rises with the water and reads a constant.

**Not autonomous, and we will not call it that.** There is no GNSS indoors and
no wheel odometry on a hull, so there is no position estimate. `pose` is null
in every real unit frame and the console draws no marker rather than a made-up
one. The decision is autonomous. The locomotion is commanded.

**Not machine learning.** The classifier is threshold logic over depth,
turbidity and disturbance. The model call lives in the cloud path, and the
cloud path is the one we cut.

**A number we retracted.** Earlier notes quoted 1.22 m of dead-reckoning drift.
That figure was one GPS fix's noise, not drift, because the simulator snaps the
estimate onto truth-plus-noise on the same tick arrival fires. Re-measured:
drift alone is 0.02 m over a 290-second run, and that is a floor rather than a
hardware result, since the simulator hands the estimator the rover's own
calibration constant. The correction and the reasoning are both in the README.

---

## What we would do next

Calibrate the mount height against a surveyed reference rather than a tray.
Put the unit on its own board and run the link end to end in water. Replace the
reactive avoidance with something that uses the full 8x8 field instead of the
centre band. And solve the sensing problem properly: a still water surface is
close to a mirror at normal incidence, which is the least obvious and most
interesting difficulty in the whole build.

---

## Sponsor tracks

Two, because two have code.

- **Arduino / Touch Grass** — `firmware/uno_q/`, flashed and streaming real
  depth and disturbance; `firmware/boat/`, compiled and boot-tested.
- **OpenAI** — `dispatch/triage.py`, a real call whose failure is a real
  network failure.

We cut five tracks that existed only as a line in a planning document. A
repository that claims a track it did not build is a repository anyone can
disprove in ten seconds, and every other claim on the page gets read
differently afterwards.

---

## Repository

https://github.com/ArseniiChan/Tikbalang

## Team

Kevin Chen, Anjelo Go, Aiaru Naukenova, Arsenii Chan.
