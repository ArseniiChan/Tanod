# Demo script

Four minutes. Two people at the table: one talks, one drives the laptop and the
water. Everything in **Act 2** works tonight with no boat and no second board.

Rule for the whole demo: **do not claim anything the screen is not showing.**
The project's one differentiator is that its claims are checkable. Overclaiming
once destroys that and there is no recovering it in a four-minute slot.

---

## Pre-flight, 20 minutes before

Run these in order. If any step fails, fix it before the judges arrive, not
during.

**1. The API key.** Without it the central panel says "cloud reachable, but no
OPENAI_API_KEY is set" instead of an actual triage sentence. That is honest but
it is a much weaker moment. Arsen sets this himself:

```
export OPENAI_API_KEY=...
```

Then start the bridge **from that same shell**, or it will not see the key.

**2. Start everything.**

```
cd Tikbalang
python3 ops/ws_bridge.py --serve-only
python3 ops/serial_forward.py --port /dev/cu.usbmodemXXXX --host 127.0.0.1
```

**3. Verify, do not assume.**

```
curl -s localhost:8767/health
curl -s localhost:8767/state/node-01
```

`health` must list `node-01`. `state/node-01` must show `age_s` under 1 and
`stale: false`. If `age_s` climbs, the forwarder is not running.

**4. Confirm the cloud path answers.**

```
curl -s localhost:8767/state/node-01 \
  | python3 -c 'import json,sys;print(json.dumps(json.load(sys.stdin)["frame"]))' \
  | curl -s -X POST -H "Content-Type: application/json" -d @- localhost:8767/triage
```

You want `"decided_on": "cloud"` and a real sentence in `text`. If it says
`source: "probe"`, the key is not in the bridge's environment.

**5. Open the dashboard at `http://127.0.0.1:8767/?src=live`.** Use that URL
every time. The source is remembered in localStorage and a stray keypress
switches it to canned REPLAY, which you must never be caught demoing.

**6. Dry-run the water once, end to end.** Pour until the state changes, cut the
network, restore it, drain. If you have not done this at least once, do not do
it for the first time in front of a judge.

**7. Leave the dashboard tab in the foreground.** A hidden tab freezes the
render loop. It resumes on focus, but a frozen screen in the first ten seconds
is not a recoverable opening.

---

## Act 1 — the problem, 30 seconds

> In a flood, the towers go down first. Every delivery and dispatch system we
> could find phones home to decide what to do next.
>
> 1.81 billion people are directly exposed to flood depths over 15 centimetres
> in a 1-in-100-year event, and 89% of them are in low- and middle-income
> countries. That is the Rentschler study in Nature Communications, 2022.

Do not add a second statistic. One number, cited, then move.

---

## Act 2 — the thing that works, 2 minutes

This is the demo. Everything below has been verified running.

**Beat 1. Show the node.** Point at the board on the table.

> This is an Arduino UNO Q. The classifier runs on its microcontroller. It is
> the same C++ file our simulator compiles, symlinked, so the board and the
> simulator cannot disagree about what counts as impassable.

**Beat 2. Pour the water.** Watch the panel go from DRY through PASSABLE to
IMPASSABLE. Let the screen do the talking for a few seconds.

> Nothing here is a recording. That reading is a laser rangefinder looking down
> at the water. We chose non-contact on purpose: a probe in the water fails by
> corroding, and a corroded electrode reports DRY while the road is under
> water. That is a confident false negative, and it is the worst failure this
> system can have. A rangefinder can only fail by going silent, and we can
> detect silence.

**Beat 3. Cut the network.** Press CUT NETWORK.

> That did not flip a variable in the browser. It told the bridge to stop
> letting triage reach the internet.

Point at the central panel showing its real error. Then point at the edge panel
still counting decisions.

**Beat 4. The payoff.** Scroll the raw frame tail.

> Every frame carries a field called `decided_on`. It is never hardcoded. It
> has printed `cloud` and it is printing `device` right now, with the uplink
> down. That is the whole claim, and you can read it off the wire.

**Beat 5. Fault, if you have 20 spare seconds.** Cover the sensor or unplug it.

> Sensor fault does not read as dry. It reads as UNKNOWN, and the planner
> refuses to route a unit through unsurveyed water.

---

## Act 3 — the unit, 45 seconds

Pick the branch that is true at 10:00. Do not improvise between them.

### Branch A — the hull is in the water

> The surface unit reads an 8x8 time-of-flight array and runs the same
> avoidance code our simulator runs, unchanged. When the node says impassable,
> the link process writes `hold` down its serial port and the unit stops.
>
> It is not navigating to a waypoint, and we are not going to say it is. There
> is no GPS indoors and no odometry on a hull, so `pose` is null in every frame
> and the map draws no marker. The decision is autonomous. The locomotion is
> commanded.

### Branch B — the hull is not moving

> The unit firmware is written and it compiles. We flashed it to the board and
> watched it boot, report `lidar not responding`, and refuse the `go` command,
> which is the behaviour we wanted. What it needs is a second microcontroller,
> because the node has to stay fixed on the bank. A depth sensor that floats
> rises with the water and reads a constant.

Branch B is a fine answer. Say it at normal speed and move on. Judges see
unfinished hardware every year. What they rarely see is a team that can say
precisely what is done and what is not.

---

## Act 4 — close, 20 seconds

> The README lists two sponsor tracks because two have code. We cut five that
> only existed in a planning document.
>
> Everything we have claimed in four minutes is either on the screen or in the
> repository, and you can check all of it.

---

## Questions you should expect

**"Is that dead-reckoning drift number real?"**
No, and the README says so. The 1.22 m figure in our earlier notes was one GPS
fix's noise, not drift. Simulated drift alone is 0.02 m over a 290-second run,
and that is a floor rather than a hardware measurement, because the simulator
hands the estimator the rover's own calibration constant.

**"Is the classifier machine learning?"**
No. It is threshold logic over depth, turbidity and disturbance. Calling it AI
would be the easiest thing in this demo to disprove. The model call is in the
cloud path, and that path is the one we cut.

**"So how is this different from a flood gauge?"**
A gauge reports. This decides, and it keeps deciding when the uplink is gone.
The field that proves it is in every frame.

**"What is the weakest part?"**
Two things. The unit needs a second board. And water is a hard target for a
time-of-flight sensor, because a still surface is close to a mirror, so we had
to [name the fix you actually used: scatterer in the water, or a float].

Answer that last one straight. A team that names its own weakest point is far
more credible than one that has to be caught.

---

## Failure drills

**The board unplugs.** `serial_forward.py` reopens the port on its own. Say
"that is the forwarder reconnecting" and keep going. `age_s` climbing on
`/state` is the tell.

**The dashboard looks frozen.** Check the tab is in the foreground. The render
loop is driven by `requestAnimationFrame` and a background tab suspends it.

**The water does not read.** Fall back to the fault story: cover the sensor and
demo UNKNOWN instead of the depth ladder. The offline cut still works and that
is the real claim.

**Everything is down.** REPLAY mode plays a recorded mission. Say the word
"recorded" out loud before you press it. Never let a judge discover that
themselves.
