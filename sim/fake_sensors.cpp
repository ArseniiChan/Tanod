#include "fake_sensors.h"
#include <cmath>
#include <cstdlib>

void FakeRover::step(const MotorCommand& cmd, double dt_s) {
  double vl = cmd.left  * max_speed_mps;
  double vr = cmd.right * max_speed_mps;

  double v = (vl + vr) * 0.5;
  // Compass convention: heading increases clockwise, so left-faster turns right.
  double omega_dps = ((vl - vr) / track_width_m) * (180.0 / M_PI);
  last_gyro_dps = omega_dps;

  heading_deg = wrap360(heading_deg + omega_dps * dt_s);
  double ds = v * dt_s;
  if (std::fabs(ds) > 1e-9) truth = project(truth, heading_deg, ds);

  // Carry the fractional remainder. Truncating every step would bleed a
  // one-directional bias into odometry that a real wheel does not have.
  if (m_per_tick > 1e-9) {
    frac_l += (vl * dt_s) / m_per_tick;
    frac_r += (vr * dt_s) / m_per_tick;
    long wl = (long)frac_l; long wr = (long)frac_r;
    ticks_l += wl; ticks_r += wr;
    frac_l -= wl;  frac_r -= wr;
  }
}

double FakeRover::front_distance_m() const {
  if (!has_obstacle) return sensor_max_m;
  double d = haversine_m(truth, obstacle);
  if (d > sensor_max_m) return sensor_max_m;
  double b = bearing_deg(truth, obstacle);
  double off = std::fabs(heading_error_deg(heading_deg, b));
  if (off > sensor_cone_deg) return sensor_max_m;  // outside the beam
  return d;
}

GeoPoint FakeRover::gps_fix() const {
  // About 2m of wander, which is roughly what a NEO-6M gives you in the open.
  double n1 = ((std::rand() % 2001) - 1000) / 1000.0;
  double n2 = ((std::rand() % 2001) - 1000) / 1000.0;
  return project(project(truth, 0, n1 * 2.0), 90, n2 * 2.0);
}
