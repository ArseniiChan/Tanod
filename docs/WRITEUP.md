# Tikbalang — submission writeup

Paste-ready. Trim from the bottom to fit the field. Every claim below is
checkable against the repository, the screen, or a cited source.

---

## One line

The Philippines already has 600+ water level sensors. They report over SMS, and
SMS goes down in the storms that matter. Tikbalang decides on the chip instead,
and shows the answer at the crossing with no network at all.

---

## The problem, stated precisely

The naive pitch would be "the Philippines needs flood sensors." That is false
and a judge can disprove it in thirty seconds. DOST-ASTI's PhilSensors network
has over 2,000 deployed devices, including **614 water level stations and 1,204
rain gauges**, with public near-real-time data. PAGASA operates another 171
water level stations and 195 rain gauges and runs flood forecasting for 13
river basins.

The problem is the **link**, and it is documented by the Asian Development Bank
in its own project appraisal:

> "ASTI stations use SMS for data transmission, and disconnection of SMS during
> storms and heavy rainfalls makes it difficult to monitor rainfall during the
> weather events when those data are most needed."
>
> — ADB, *Integrated Flood Resilience and Adaptation Project*, linked document

That is a development bank naming our exact premise as a live defect in the
deployed national network. The sensors are fine. The assumption that they can
phone home during a typhoon is not.

The failure is not theoretical. During **Typhoon Tino (Kalmaegi)** in November
2025, the NDRRMC reported communication problems in **all ten affected regions
that were reporting at all**, alongside **42 of 69 road sections impassable**.
During **Super Typhoon Uwan (Fung-Wong)** days later, 155 cities and
municipalities lost power and 71 roads were still impassable. Together the two
storms affected nearly **nine million people**, killed **259**, and displaced
**2,311,340** as of the NDRRMC's 13 November report; the President declared a
state of national calamity. In Cebu five days after landfall, a local report
described rescuers in Balamban struggling to coordinate because the cellular
network was still down.

This is not a one-storm anomaly. After **Typhoon Odette (Rai)** in December
2021, 135 municipalities lost telecommunications and 40 still had none two
weeks later. The UN Emergency Telecommunications Cluster did not stand down
until **31 March 2022**, three and a half months after landfall.

And a quarter of the fleet is already dark on a clear day: a 2023 Commission on
Audit report found that of 1,825 PAGASA hydromet stations inspected, **246
(13.5%) were not operational and not rehabilitatable** and another **184
(10.1%) needed rehabilitation**, with over ₱256 million of equipment idle and
no longer sending data to the central server.

Meanwhile the decision people actually make, many times a day during a flood,
is small and local: **is this crossing passable right now.** Today that is
answered by LGU advisories on social media, news bulletins, Waze reports and
crowdsourced maps. Every one of those requires the network that just went down.

---

## What we built

A node that answers that one question where the water is, decides it on its own
microcontroller, and does not need anything downstream to be alive.

**The classifier runs on the chip.** `node/classify.cpp` compiles unchanged for
three targets: the laptop test harness, the simulator, and the STM32U585 on an
Arduino UNO Q. The firmware sketch does not contain a copy of it; it symlinks
the same file, so the board and the simulator cannot drift apart about what
counts as impassable. It reads depth from a downward-looking laser rangefinder
and disturbance from an IMU.

**Non-contact by design.** A probe in the water fails by corroding, and a
corroded bottom electrode reports DRY while the road is under water. That is a
confident false negative and it is the worst failure this system can produce. A
rangefinder can only fail by going silent, and silence is detectable: an
unavailable sensor sets the hazard to UNKNOWN rather than DRY, and the planner
refuses to route anything through unsurveyed water.

**The answer is delivered where the network is not.** This is the question that
kills most offline-inference projects, so we built three answers rather than
claiming one:

1. *At the crossing, with nothing else alive.* The node blinks its verdict on a
   2-second cycle. One blink is passable, two is passable but foul, a fast
   pulse train is impassable, and long-short-long is a sensor fault. IMPASSABLE
   is the only fast pattern on purpose: the state that must never be mistaken
   for another one reads differently from across a road. This works with the
   uplink down, the laptop gone and the console closed.
2. *Locally, with no internet.* The bridge binds `0.0.0.0` and serves the
   console itself, so any phone on the same local network sees live verdicts
   with no route to the outside world.
3. *Not built, and we will say so.* LoRa to a barangay hall, or the UNO Q's own
   Wi-Fi as a soft access point at the crossing. Designed, not implemented.

**The offline claim is checkable, not asserted.** `dispatch/triage.py` makes a
real HTTPS call to a real model with a short timeout, and falls back to the
verdict the node already reached. Every frame carries `decided_on`, reading
`device` or `cloud`, never hardcoded. Cut the uplink on stage and the cloud
panel returns a real network error while the edge panel keeps counting
decisions. You can read the field off the wire.

**The plumbing.** `ops/ws_bridge.py` is Python standard library only, no
dependencies: WebSocket for consoles, TCP line ingest for producers, REST plus
an OpenAPI 3.1 document. Each console gets a bounded queue and its own sender
thread, so one stalled browser cannot block the producer. We found that the
hard way, and the regression test for it is in `ops/stress.py`.

---

## What we tested

`ops/stress.py` is an adversarial suite that runs the whole system with **no
hardware attached**, because the software path is the one that has to survive a
board that will not enumerate at 9am. Each case gets a fresh bridge, because a
suite that reports failures it cannot reproduce in isolation is worse than no
suite.

Nineteen cases pass. Among them: malformed, truncated, non-UTF-8 and
newline-free input on the producer port leaves the bridge up and still
accepting good frames afterwards; a console that connects and never reads
cannot stall a producer running at 400 times the demo rate; four threads
opening and closing consoles as fast as they can does not stall the producer or
leak clients; an unreachable cloud gives up inside its timeout and still
returns a usable verdict marked `device`; a 10 MB POST body is refused without
taking the connection with it; and the cut-network command latches and releases
as real state on the bridge rather than as a variable in the browser.

Measured sustained ingest with a console attached: **about 70,000 frames per
second**, roughly 14,000 times the 5 Hz demo rate. `ops/preflight.sh` runs the
subset that matters before a demo and prints the fix for anything that fails.

---

## What is real and what is not

We would rather say this ourselves than have it found.

**Real and running:** the node is flashed and streaming genuine readings at
5 Hz, and signals its verdict locally with no network. The cloud call is real
and its failure is real. `decided_on` has printed both values in one session.
The bridge, the console and the stress suite run.

**Not real:** the surface unit has never run with its sensor attached, because
the node occupies our only board and the node has to stay fixed on the bank. A
depth sensor that floats rises with the water and reads a constant. The unit
firmware compiles and was flashed to real silicon, where it boots, reports
`lidar not responding`, and correctly refuses to run.

**Not autonomous, and we will not call it that.** There is no GNSS indoors and
no wheel odometry on a hull, so there is no position estimate. `pose` is null in
every real unit frame and the console draws no marker rather than a made-up
one. The decision is autonomous. The locomotion is commanded.

**Not machine learning.** The classifier is threshold logic over depth,
turbidity and disturbance. The model call lives in the cloud path, and the
cloud path is the one we cut.

**Depth only, and that is a real limitation.** Flood hazard is depth ×
velocity, not depth. The UNSW Water Research Laboratory's criteria, which
underpin Australian flood hazard guidance, put a small car's limit at 0.3 m in
still water but **0.1 m in fast flow**, with a stability threshold of
D×V ≤ 0.3 m²/s. Our node measures depth and a crude disturbance proxy from the
IMU, not velocity. A depth-only classifier will be wrong in both directions on
fast water, and the honest fix is a velocity term, not a tighter threshold.

**Our thresholds are rig-scale, not road-scale.** The bench rig resolves 10 mm
per rung over a 60 mm span and calls 30 mm impassable, which is correct for the
small ground unit we built and far too conservative for a car. A field unit
would use bands people already know: the MMDA's public flood gauge marks 8–10
inches passable to cars, 13–19 inches not passable to light vehicles, and
26–45 inches not passable to anything. Matching those is a constant change, not
a redesign, but we have not made it and are not claiming it.

**A number we retracted.** Earlier notes quoted 1.22 m of dead-reckoning drift.
That figure was one GPS fix's noise, not drift, because the simulator snaps the
estimate onto truth-plus-noise on the same tick arrival fires. Re-measured,
drift alone is 0.02 m over a 290-second run, and even that is a floor rather
than a hardware result, since the simulator hands the estimator the rover's own
calibration constant. The correction is in the README.

---

## Why this is not already solved

There is a real body of Philippine work on LoRa-based and offline-capable flood
early warning, and we are not claiming to have invented offline sensing. Our
difference is narrower and we will state it narrowly: existing stations are
sited on rivers and in basins to feed forecasting models, and they answer
"what is the river doing." We answer "can a person cross this, now," at the
crossing, on the chip, and we show it there without a network.

---

## What we would do next

Add a velocity term so hazard is D×V rather than depth. Rescale the bands to
the MMDA gauge so the output is legible to a Filipino driver without
translation. Put the surface unit on its own board. Solve the sensing problem
properly: a still water surface is close to a mirror at normal incidence, which
is the least obvious difficulty in the whole build.

---

## Sponsor tracks

Two, because two have code.

- **Arduino / Touch Grass** — `firmware/uno_q/`, flashed to an UNO Q, running
  the classifier on the STM32U585 and turning depth and IMU data into a verdict
  and a local signal rather than a chart.
- **OpenAI** — `dispatch/triage.py`, a real call whose failure is a real
  network failure, with the device verdict as the fallback.

We cut five tracks that existed only as a line in a planning document.

---

## Sources

- ADB, *Integrated Flood Resilience and Adaptation Project* (the SMS quote):
  https://www.adb.org/sites/default/files/linked-documents/51294-001-ld-04.pdf
- DOST-ASTI PhilSensors: https://philsensors.asti.dost.gov.ph/site/about
- PAGASA flood operations: https://pagasa.dost.gov.ph/flood
- NDRRMC totals for Tino and Uwan, 13 Nov 2025:
  https://mb.com.ph/2025/11/13/ndrrmc-tino-uwan-affect-nearly-9-m-people
- State of national calamity:
  https://www.philstar.com/headlines/2025/11/07/2485389/marcos-declares-national-state-calamity
- Tino: comms out in all reporting regions, 42 of 69 roads impassable:
  https://www.gmanetwork.com/news/topstories/nation/965107/typhoon-tino-affected-almost-2m-people-ndrrmc/story/
- Uwan: 155 municipalities without power, 71 roads impassable:
  https://newsinfo.inquirer.net/2137511/fwd-ocd-155-cities-towns-without-power-amid-uwan-onslaught
- Odette: 40 of 135 municipalities still without telecoms:
  https://reliefweb.int/report/philippines/philippines-typhoon-raiodette-flash-update-no-3-20-december-2021-8-pm-local-time
- UN ETC stood down 31 March 2022:
  https://www.etcluster.org/emergency/philippines-super-typhoon-raiodette
- COA on idle PAGASA stations:
  https://www.philstar.com/headlines/2025/01/24/2416441/coa-flags-p256-million-idle-rain-stations
- UNSW WRL TR2014/07, depth × velocity flood hazard:
  https://www.unsw.edu.au/content/dam/pdfs/engineering/civil-environmental/water-research-laboratory/publications/WRL-TR2014-07-Flood-hazard.pdf
- US NWS, Turn Around Don't Drown: https://www.weather.gov/safety/flood-turn-around-dont-drown
- MMDA flood gauge bands: https://philkotse.com/market-news/mmda-flood-guide-11003

## Repository

https://github.com/ArseniiChan/Tikbalang

## Team

Kevin Chen, Anjelo Go, Aiaru Naukenova, Arsenii Chan.
