# Field node, Arduino UNO Q

Reads the water surface with a **Modulino Distance (ABX00102)** looking down at
it, reads disturbance with a **Modulino Movement (ABX00101)**, classifies the
hazard on the MCU, and prints one telemetry frame per reading to Serial.

No copy of the classifier lives here. `classify.cpp`, `classify.h`, `frame.cpp`
and `frame.h` are symlinks to `node/`. The node and the simulator compile the
same source, which is why they cannot disagree about what counts as IMPASSABLE.
Do not replace the symlinks with copies.

## Why a rangefinder and not a probe in the water

Nothing gets wet. A contact ladder fails by corroding, and a corroded bottom
electrode reports DRY while the road is under water: a confident false negative,
the worst failure this system can produce. A rangefinder can only fail by going
silent, and silence is detectable. The sketch reports an out-of-range sensor as
`rungs_wet = -1`, which the classifier turns into `UNKNOWN` at 0.00 confidence
with the sentence "treat as unsurveyed", and the dispatch planner then routes
around the crossing instead of calling it passable.

It also means the printed waterproof enclosure is doing real work: the box sits
above the waterline and the sensor looks down. "We do not put electronics in
floodwater" is a deployment answer, not a workaround.

## Calibrate before you trust a single reading

**`MOUNT_HEIGHT_MM`** in the sketch is the distance in mm from the sensor face
to the **dry** bottom of your tray. Depth is `MOUNT_HEIGHT_MM - measured`.

Measure it in the rig you will actually demo with. Get it wrong and every depth
is wrong by the same constant, which is the easiest bug here to miss because the
numbers still look entirely plausible.

Sanity check after setting it: empty tray should read 0 mm and `DRY`. Pour in
enough water to cover a finger joint and it should climb.

## The trap that costs an hour

`surface.available()` returns **false when nothing is in range**, not when the
reading is zero. A sensor pointed at open air reports nothing at all. If your
mount height puts the water surface beyond the module's range, you get silence
and conclude the hardware is dead. Wave your hand in front of it first: if
readings start, the module is fine and your geometry is wrong.

## Getting frames to the dashboard

**Tonight, the known-good path.** The sketch prints to Serial at 115200. On the
machine the UNO Q is plugged into:

```
python3 ops/serial_forward.py --host <bridge-ip>
```

That path is already built and tested end to end.

**The upgrade, if there is time.** The UNO Q runs Debian with Wi-Fi 5, so the
Linux side can POST frames straight to the bridge and the USB cable disappears
entirely:

```
curl -X POST http://<bridge-ip>:8767/ingest -d '<frame>'
```

The Python side reaches MCU data over the App Lab Bridge RPC rather than
touching I2C directly. Do this second. The serial path already works and the
demo does not care which one carries the bytes.

## Demo controls, over Serial

```
uplink off      drop the uplink flag
uplink on       restore it
infer cloud     report decisions as cloud-made
infer local     report decisions as device-made
```

All four are variables, never constants. That is the point.

## Windows

git does not create real symlinks unless `core.symlinks=true` and Developer
Mode are both on. If the four linked files look like one-line text files
containing a path, copy the real ones in from `node/` instead and remember they
are now copies that can drift.
