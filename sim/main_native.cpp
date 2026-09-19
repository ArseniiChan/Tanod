// Laptop build. Runs the exact nav/ code the ESP32 runs, against a fake rover.
// Emits the same JSON telemetry frame the firmware emits, one line per tick,
// so the dashboard can be developed and demoed with no hardware present.
//
//   make && ./build/sim | head -40
//
#include "../nav/geo.h"
#include "../nav/mission.h"
#include "../nav/deadreckon.h"
#include "fake_sensors.h"
#include <cstdio>
#include <cmath>

int main() {
  FakeRover rover;
  rover.heading_deg = 45.0;   // start aligned with the first leg

  DeadReckoner dr;
  dr.m_per_tick = rover.m_per_tick;
  dr.track_width_m = rover.track_width_m;
  dr.reset(rover.truth, rover.heading_deg);

  GeoPoint wps[2];
  wps[0] = project(rover.truth, 45.0, 40.0);
  wps[1] = project(rover.truth, 90.0, 70.0);

  // Park an obstacle directly on the leg to the first waypoint.
  rover.has_obstacle = true;
  rover.obstacle = project(rover.truth, 45.0, 15.0);

  Mission m;
  m.load(wps, 2);
  m.start();

  const double dt = 0.05;            // 20 Hz, same as the firmware loop
  for (int i = 0; i < 20000; ++i) {
    double front = rover.front_distance_m();
    bool blocked = front < 0.50;

    MotorCommand cmd = m.step(dr.pos, dr.heading_deg, blocked);
    rover.step(cmd, dt);
    dr.update(rover.ticks_l, rover.ticks_r, rover.last_gyro_dps, dt);

    // NEO-6M fixes land at about 1 Hz.
    //
    // Do not read the printed err_m as dead-reckoning drift. This correction
    // snaps the estimate straight onto truth-plus-noise, and the ARRIVED event
    // fires on the same tick, so err_m at ARRIVED is one GPS fix's noise.
    // Measured: 1.22 m with this line in, 0.02 m with it commented out.
    //
    // The correction still earns its place, just not here. Line 19 copies the
    // rover's own m_per_tick into the estimator, so calibration error is zero,
    // which no real robot enjoys. At a 5% tick error the mission ends 3.83 m
    // from the goal with this line and 5.01 m without it.
    if (i % 20 == 0) dr.correct_with_gps(rover.gps_fix());

    if (i % 20 == 0) {   // 1 Hz to stdout so it stays readable
      printf("{\"t\":%d,\"lat\":%.6f,\"lon\":%.6f,\"fix\":1,\"hdg\":%.1f,"
             "\"spd\":%.2f,\"encL\":%ld,\"encR\":%ld,\"d\":[%.0f],\"wp\":%d,"
             "\"mode\":\"%s\",\"err_m\":%.2f}\n",
             i, dr.pos.lat, dr.pos.lon, dr.heading_deg,
             (cmd.left + cmd.right) * 0.5 * rover.max_speed_mps,
             rover.ticks_l, rover.ticks_r, front * 1000.0,
             m.wp_index, m.state_name(),
             haversine_m(dr.pos, rover.truth));
    }
    if (m.state == MissionState::ARRIVED) {
      printf("{\"event\":\"ARRIVED\",\"drift_m\":%.2f}\n",
             haversine_m(dr.pos, rover.truth));
      break;
    }
  }
  return 0;
}
