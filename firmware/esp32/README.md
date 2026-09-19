# ESP32-S3 firmware

Builds the same `nav/` sources as the simulator. Nothing in `nav/` may include
an Arduino header, or the native build breaks and the test harness dies with it.

Loop runs at 20 Hz:

1. read encoders (ISR counters), IMU yaw rate, GPS fix if one arrived
2. `DeadReckoner::update`
3. `Mission::step`
4. write motor PWM, kick the watchdog
5. push a telemetry frame over WebSocket

Watchdog: if step 2 or 3 misses its deadline, drive both motors to zero before
anything else.
