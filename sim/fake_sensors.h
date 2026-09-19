#pragma once
#include "../nav/geo.h"
#include "../nav/mission.h"

// Synthetic vehicle. Consumes motor commands, produces encoder ticks,
// a gyro rate and a noisy GPS fix, at the same rates as the real rover.
struct FakeRover {
  GeoPoint truth{42.35983, -71.09211};  // Johnson Athletic Center, roughly
  double heading_deg = 0;
  double m_per_tick = 0.0015;
  double track_width_m = 0.15;
  double max_speed_mps = 0.5;

  long ticks_l = 0, ticks_r = 0;
  double frac_l = 0, frac_r = 0;   // partial ticks carry over, as real encoders do
  double last_gyro_dps = 0;

  // A real obstacle at a real place, so steering away actually clears it.
  bool has_obstacle = false;
  GeoPoint obstacle{0, 0};
  double sensor_cone_deg = 25.0;   // HC-SR04 beam, roughly
  double sensor_max_m = 4.0;

  void step(const MotorCommand& cmd, double dt_s);
  double front_distance_m() const;
  GeoPoint gps_fix() const;
};
