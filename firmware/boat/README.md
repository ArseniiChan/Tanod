# firmware/boat

The surface unit. Runs on a **second** board, not the one running `firmware/uno_q`.

The node has to stay fixed on the bank looking down at the water. A depth
sensor that floats rises with the water and reads a constant, so the node
cannot ride the boat. One board cannot be both.

## Symlinks

`avoid.cpp`, `avoid.h`, `mission.h` and `geo.h` are symlinks into `nav/`.
The avoidance behaviour the judges see is the same file the simulator runs.
If the links are missing after a clone:

    cd firmware/boat
    ln -sf ../../nav/avoid.h   avoid.h
    ln -sf ../../nav/avoid.cpp avoid.cpp
    ln -sf ../../nav/mission.h mission.h
    ln -sf ../../nav/geo.h     geo.h

`nav/mission.cpp` is deliberately NOT linked. `Mission::step` needs a position
and a heading, and this unit has neither. See "no pose" below.

## Dependencies

    arduino-cli lib install Servo

`DFRobot_MatrixLidar` is not in the Library Manager index. Clone it into your
sketchbook libraries folder:

    git clone https://github.com/DFRobot/DFRobot_MatrixLidar \
      ~/Documents/Arduino/libraries/DFRobot_MatrixLidar

## Build and flash

    export TMPDIR=/tmp        # arduino-cli's ctags fails without this on macOS
    arduino-cli compile --fqbn arduino:zephyr:unoq firmware/boat
    arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn arduino:zephyr:unoq firmware/boat

Measured on an UNO Q: 93784 bytes flash (11%), 38868 bytes globals (14%).

## Wiring

| Part | Where | Note |
|---|---|---|
| SEN0628 | I2C `0x33` | Gravity is JST-PH 2mm, UNO Q Qwiic is JST-SH 1mm. Adapter or four jumpers: VCC 3V3, GND, SDA, SCL. |
| Fin servo L | `D9` signal | |
| Fin servo R | `D10` signal | |
| Servo power | battery, **not** the board | An MG996R stalls near 2.5A and will brown out the MCU. Tie battery ground to board ground. |

## Running it

    python3 ops/link_boat.py     --port /dev/cu.usbmodemBOAT     # open this FIRST
    python3 ops/serial_forward.py --port /dev/cu.usbmodemBOAT --host 127.0.0.1

`link_boat.py` opens the port first on purpose. Opening a USB CDC port toggles
DTR and resets the board, and you want that reset before the demo, not during.

Serial commands: `go`, `hold`, `ping`.

## No pose

Every frame carries `"pose": null`. There is no GNSS indoors and no wheel
odometry on a hull, so there is no position estimate to report. The dashboard
renders this as `unit-01 · MODE · no position fix` and draws no marker.

Say it out loud in the demo: **the decision is autonomous, the locomotion is
commanded.** The unit reacts to obstacles and to the node's verdict. It does
not navigate to a waypoint. Printing coordinates nobody measured would be the
one lie in an otherwise honest system.

## Failure behaviour

`obstacle_mm` is `null` when no zone returns a usable range. That is a fault,
and a fault stops the unit. It is never read as open water. `go` will not
start the fins while the lidar is silent, which is verified on hardware: with
no SEN0628 attached the board boots, reports
`"fault":"lidar not responding"` at 5 Hz, and ignores `go`.
