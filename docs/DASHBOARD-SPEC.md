# Dashboard spec. 15:05 Sat.

The dashboard IS the demo. A judge standing three feet away for eight minutes
understands the project entirely through this screen. Runs on the ASUS ZenScreen.

Design rule: every element proves something. If it only decorates, cut it.

---

## The data contract  (agree this FIRST, then everyone builds in parallel)

Node and robot both publish this frame over WebSocket, 5 Hz from the node,
20 Hz from the robot. Dashboard never reads hardware directly.

```json
{
  "src": "node-01",
  "t": 1789840000,
  "link": { "uplink": true, "inference": "local" },
  "water": { "rungs": 6, "wet": 3, "mm": 30 },
  "tds": { "ppm": 412, "trend": "rising" },
  "temp_c": 18.4,
  "sound": { "event": "none", "conf": 0.0 },
  "hazard": { "state": "SHALLOW_CROSSING", "conf": 0.86,
              "why": "3 of 6 rungs wet, turbidity rising" },
  "decided_at": 1789840000,
  "decided_on": "device"
}
```

```json
{
  "src": "unit-01",
  "t": 1789840001,
  "pose": { "lat": 42.35983, "lon": -71.09211, "hdg": 45.1 },
  "mode": "DRIVING",
  "wp": 1,
  "obstacle_mm": 494,
  "mission": "m-7",
  "battery_pct": 87
}
```

Two rules that make this work:
- `decided_on` is `"device"` or `"cloud"`. The dashboard renders it. This is the
  whole thesis, made visible, in one field.
- `why` is a plain sentence the node generates. Never a code. Judges read it.

---

## MUST HAVE. Without these the demo does not work.

1. **HAZARD STATE, huge.** One word, colour coded, readable across a table.
   SHALLOW CROSSING / CONTAMINATED / IMPASSABLE. With the `why` line under it.
2. **Water level as a STEP gauge, six discrete rungs.** Not a smooth curve. Our
   sensor is a ladder with six electrodes, so a smooth line would be a lie.
   Showing the real resolution reads as competence.
3. **CUT NETWORK toggle the judge operates themselves.** Big, obvious, physical
   feeling. This is the single most important control on the screen.
4. **Split screen: CENTRAL vs EDGE.** Same flood, both panels. On cut, the
   central panel freezes with "awaiting dispatch server" and ours keeps updating.
5. **Map.** Node position, target, chosen route. 2D top down. Canvas or SVG.
   Not 3D. 3D will eat the night and score nothing extra.
6. **Raw telemetry tail.** A scrolling monospace feed of the actual frames.
   Judges trust what they can see arriving. Canned data has no tail.
7. **Inference badge.** "RUNNING LOCAL — ASUS Ascent GX10" vs "CLOUD". Reads
   straight off `decided_on`.

## SHOULD HAVE. These are what make it impressive rather than adequate.

8. **Rejected routes, with reasons.** Show the two routes we did NOT pick and
   why: "south crossing 0.6m, exceeds passable depth." Decision transparency is
   rare in hackathon projects and judges notice it immediately.
9. **Flood risk overlay** from the Voloridge dataset, under the map.
10. **Voice transcript** appearing live as Deepgram returns it.
11. **Mission state machine**: REQUESTED → TRIAGED → DISPATCHED → EN ROUTE →
    DELIVERED. Shown as a row of stages, current one lit.
12. **Robot pose and trail** on the map.
13. **Decision latency**, node and dispatch. Small numbers, bottom corner.

## NICE. Only if the above is done and rehearsed.

14. Elastic-backed event search a judge can type into.
15. Acoustic event markers on the timeline.
16. Battery and uptime.

## DEMO INSURANCE. Build this, it costs an hour.

17. **REPLAY MODE.** Play back a recorded mission from a JSON file, with a
    visible "REPLAY" badge so we never misrepresent it. If the hardware dies at
    12:40 Sunday we still have a demo, and we say plainly that it is a replay.

---

## Build notes

- Plain JS plus Canvas. No React. A framework costs setup time and buys nothing
  for one screen.
- Dark ground, very large type. It is read from three feet away by someone
  standing up, not from a laptop.
- Semantic colour is separate from brand colour: green passable, amber caution,
  red impassable. Never encode state in colour alone, always put the word too.
- It must render correctly with NO data arriving. Empty state is "AWAITING NODE",
  not a blank screen or a crash.
- Start against `./build/sim` output today. The frame shape above is close enough
  to what the simulator already emits that the map and tail work immediately.

## Order of work

1. WebSocket in, raw tail out. Proves plumbing.  (30 min)
2. Hazard block and step gauge.                  (1 hr)
3. Cut-network toggle and the split screen.      (2 hrs)
4. Map with route.                               (2 hrs)
5. Rejected routes and the why panel.            (1 hr)
6. Voice transcript and mission stages.          (1 hr)
7. Replay mode.                                  (1 hr)
