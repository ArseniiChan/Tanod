# Field node sketch

Reads a water ladder, classifies on the chip, prints one telemetry frame per
reading to Serial at 115200.

This folder contains **no copy of the classifier**. `classify.cpp`, `classify.h`,
`frame.cpp` and `frame.h` are symlinks to the real sources in `node/`. The
board and the simulator compile the same code, so they cannot disagree about
what counts as IMPASSABLE. That property is worth protecting: do not replace
the symlinks with copies.

## Wiring, per rung

```
3V3 ---[ probe A ]~~ water ~~[ probe B ]--- GPIO --- 10k --- GND
```

The 10k resistor to ground is not optional. Without it the pin floats and
reports noise. Water bridging the probes pulls the pin up.

Six rungs, 10 mm apart vertically. The spacing has to match `RUNG_SPACING_MM`
in `node/classify.cpp` or the reported depth is wrong.

Default pins are GPIO 1 through 6 for the rungs and GPIO 7 for TDS. These are
**ADC1** pins on an ESP32-S3. Do not move them to ADC2: that block shares
hardware with the WiFi radio and stops returning usable values once WiFi is up,
which is an unpleasant thing to discover during a demo.

## Calibrate before trusting it

Set `CALIBRATE` to 1, flash, and open the serial monitor. It prints raw ADC
values instead of frames.

1. Read the numbers with the probes dry. Expect something low.
2. Put the probes in the water you will actually demo with.
3. Set `wet_threshold` to the midpoint and set `CALIBRATE` back to 0.

Do this with the demo water, not distilled and not a different cup. Tap water,
bottled water and water with a pinch of salt read very differently, and a
threshold tuned on one will misread another.

## Arduino IDE setup

Board: ESP32S3 Dev Module (or ESP32-S3-Box for that board).
Upload speed 115200. The sketch folder name and the `.ino` name must match,
which they do.

**On Windows**, git does not create real symlinks unless `core.symlinks=true`
and Developer Mode are both on. If the four files here look like one-line text
files containing a path, that happened. Copy the real files in instead:

```
copy ..\..\node\classify.cpp .
copy ..\..\node\classify.h .
copy ..\..\node\frame.cpp .
copy ..\..\node\frame.h .
```

and remember they are now copies that can drift.

## Sending the frames to the dashboard

The sketch only prints to Serial. Getting those lines onto the shared dashboard
is `ops/serial_forward.py`, run on whichever machine the board is plugged into:

```
python3 ops/serial_forward.py --host <bridge-ip>
```

## Cutting the uplink

Send `uplink off` on the serial line and the `uplink` field goes false. `uplink on`
puts it back. It is a variable, never a constant, which is the point.
