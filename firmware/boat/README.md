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

## Drive

`PADDLE_WHEELS` at the top of `boat.ino` selects the drive. All three compile.

| Value | Drive | Steering |
|---|---|---|
| `0` | fins flap, no motor | flap amplitude differential |
| `1` | **one paddle wheel (this build)** | fins held as rudders |
| `2` | two paddle wheels | true differential, fins centred |

The motor is an OSEPP 25 mm brushed gearmotor, 1:45, roughly 200 RPM at the
output shaft. That is far too slow for a propeller and about right for a paddle
wheel, so the hull is a riverboat. Print the wheel with a 50 to 70 mm paddle
radius; bigger bites more water but loads the gearbox harder.

One thing about `PADDLE_WHEELS 1` that matters in the water: **a rudder only
bites while water is moving past it, so the hull cannot pivot in place.**
`avoid_step()` reverses before it turns, which is what makes the behaviour still
work: the hull backs out of the obstacle first, then steers.

## Wiring

| Part | Where | Note |
|---|---|---|
| SEN0628 | I2C `0x33` | Gravity is JST-PH 2mm, UNO Q Qwiic is JST-SH 1mm. Adapter or four jumpers: VCC 3V3, GND, SDA, SCL. |
| H-bridge L | `IN1 D4`, `IN2 D5`, `PWM D3` | TB6612FNG or L298N. A DRV8871 takes two PWM pins instead and needs `motor_write()` rewritten. |
| H-bridge R | `IN1 D6`, `IN2 D7`, `PWM D11` | Only used when `PADDLE_WHEELS 2`. |
| Fin servo L | `D9` signal | |
| Fin servo R | `D10` signal | Set `FIN_R_SIGN` to `-1` if the horns are mirrored and the rudders fight each other. Check this dry. |
| Motor and servo power | battery, **not** the board | An MG996R stalls near 2.5A and will brown out the MCU. Tie battery ground to board ground. |

`PWM_MIN` is 60. Below roughly that duty a geared motor buzzes without turning,
which sounds like a fault and wastes current, so any nonzero command is lifted
to it. At zero the bridge coasts rather than brakes: a braked paddle wheel in
water is a rudder nobody asked for.

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
