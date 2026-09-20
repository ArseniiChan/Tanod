# How "offline" actually works. 16:00 Sat.

## The trap

If the node, the dashboard and the rover all talk over the venue wifi, then the
"cut the network" moment is theatre. Worse, cutting it kills our own telemetry
and the dashboard goes blank, which reads as a crash, not resilience.

A sharp judge asks one question and the demo collapses:

> "Your node still has to reach your laptop. That is a network. So what exactly
> is offline here?"

We need an answer that is true, not clever.

## The distinction that makes it true

Two different networks, and only one of them dies in a flood.

| | What it is | Dies in a flood? | In our demo |
|---|---|---|---|
| **Uplink** | internet, cloud APIs, a central dispatch server | YES. Grid fails, towers follow. | This is what we cut. |
| **Local link** | the system's own radio between its own devices | No. It is our infrastructure, not the carrier's. | Never cut. |

Deployed disaster tech already works this way. A mesh of local devices talking to
each other over their own radio IS offline in the sense that matters. What kills
Zipline and every centralised dispatcher is losing the uplink, not losing the
local link.

So the claim is precise: **no decision in this system requires the uplink.**

## Build it so the claim is physically verifiable

### 1. The node runs its own access point
ESP32-S3 in softAP mode. SSID `TANOD-NODE`. The laptop joins THAT, not
`HackMIT.2026`. A few lines of setup.

Why this matters: a judge can look at the laptop's wifi menu and see we are not
on the venue network at all. The local link has never touched the internet.
That is checkable, and checkable beats asserted.

Stronger if there is time: **ESP-NOW** peer to peer, no access point at all.
Node talks straight to a second ESP32 plugged into the laptop over USB serial.
Then there is no wifi infrastructure in the path whatsoever.

### 2. Inference runs on the GX10, over ethernet or USB
Local box, local cable. The triage and routing model never leaves the table.

### 3. The rover joins the same local island
Go2 has its own wifi, dimOS talks to it directly. Simulated rover runs on the
laptop and needs no network at all.

### 4. The "central dispatch" panel is REAL, and really cloud
This is the part most teams fake. Do not fake it.

Left panel genuinely calls a cloud API. Right panel genuinely uses the local
model. When the uplink drops, the left panel genuinely fails, because it
genuinely needed something that is no longer there.

Nothing is simulated. The failure is real, which is why it is convincing.

### 5. What the CUT NETWORK toggle does
It drops the uplink only: disable the laptop's internet interface, or block the
cloud endpoint. The local link stays up, so our telemetry keeps flowing and the
dashboard keeps updating while the centralised panel dies.

Best version: do it physically. Pull the ethernet, or toggle the interface in
front of them. Physical beats a button a judge might assume is scripted.

## The story, checked end to end

1. A flood takes the grid. Towers run on battery, then follow.
2. Responders are blind, and submerged roads stop the heavy machinery.
3. EMILY needs a human on a remote. Zipline needs somewhere to land and a
   network to dispatch it.
4. Our node senses locally and decides on the chip.
5. Our dispatch triages and routes locally.
6. Our rover carries power in, because power is what brings comms back.
7. Cut the uplink. All of it keeps working.

Every link follows from the one above it. No step needs the internet.

## The three questions a judge will ask

**"Isn't your local link still a network?"**
Yes, and it is ours, not the carrier's. That is the point. A deployed system
carries its own radio. What fails in a disaster is the uplink to the outside,
and nothing we do needs it. Here, look at the wifi menu.

**"Why ground, not a drone?"**
Zipline cannot land in a flooded street, and an aircraft cannot put a sensor in
the water to tell you whether the crossing is passable. We can do both.

**"Why power banks?"**
Because power is what restores communication. Everything else waits on that.

## What to fix tonight

- Node in softAP mode, laptop joined to it, venue wifi off on that machine.
- One genuinely cloud-dependent path so the failure is real.
- Rehearse the cut as a physical action, not a button.
