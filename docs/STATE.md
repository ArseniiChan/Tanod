# Tikbalang — state of play

Last updated: Sat 19 Sep 2026, 16:20 EDT.

## What runs today

| Piece | Status |
|---|---|
| `nav/` + `sim/main_native.cpp` | Two-waypoint mission with obstacle avoidance completes. 1.22 m dead-reckoning drift with 1 Hz GPS correction. |
| `node/` + `sim/node_native.cpp` | 88-frame script, all four hazard states, uplink flips false at frame 40 and frames keep coming. Flags: `--rate`, `--once`, `--hold`. |
| `dashboard/index.html` | Single-file Canvas console. Renders correctly empty, mock and cut. |
| `ops/ws_bridge.py` | Dependency-free WebSocket server on `:8765`. Handshake, server frames, and the dashboard's masked `cut_network` frame all verified. |

`nav/` and `node/` are plain C++ with no platform headers, so the same sources build natively and cross-compile for the ESP32-S3. The simulator and the firmware cannot drift apart.

## Running the demo

```
rm -rf build && make
python3 ops/ws_bridge.py ./build/node --rate 5
```

Open `dashboard/index.html`, pick LIVE. The bridge holds the node until the dashboard connects, so the scripted uplink failure never happens off-screen.

Piping also works but drops frames emitted before the dashboard connects. At 5 Hz the uplink dies 8 seconds in, so a late connect means the cut is already over:

```
./build/node --rate 5 | python3 ops/ws_bridge.py
```

## Offline demo design

Cutting the venue wifi is theatre and reads as theatre. Instead:

- The node runs softAP, so its network verifiably never touches the internet.
- The "central" panel genuinely calls a cloud API, so its failure is real rather than simulated.
- `decided_on` is `"device"` or `"cloud"` and is never hardcoded.

## Open items

1. The node has never read a real sensor. Everything so far is synthetic. Largest remaining credibility gap.
2. No demo video recorded as insurance against a hardware failure on stage.
3. The Dimensional booth question is still unanswered. They lent 20 Unitree Go2 dogs and dimOS runs in MuJoCo simulation, the only mobility path needing no parts.

## Gotchas

- A Linux binary left in `build/` gives `zsh: exec format error` on macOS. `rm -rf build && make`.
- Heading is a compass convention: it increases clockwise, so `omega = (vl - vr) / L`. The textbook form has the opposite sign and makes the rover spin in place forever.
- Steering authority is capped at `max_turn = 0.35`. At a 0.15 m track width, full differential is roughly 380 dps and the mission oscillates.
- Encoder ticks carry a fractional remainder between steps. Integer truncation alone bleeds about 2 m of one-directional drift.
- The enclosure is `w100 d100 h40`, not `w80 d80 h30`. An 84 mm half breadboard does not fit in an 80 mm box. Print time is 3 to 4 hours.
