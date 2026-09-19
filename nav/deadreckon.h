#pragma once
#include "geo.h"

// Fuses wheel odometry with an IMU heading. The MPU-6050 has no magnetometer,
// so gyro heading drifts. The complementary filter leans on the gyro for short
// term response and on encoder-derived heading for long term correction.
struct DeadReckoner {
  GeoPoint pos{0, 0};
  double heading_deg = 0;      // current best heading estimate
  double track_width_m = 0.15; // distance between the two drive wheels
  double m_per_tick = 0.0;     // set from calibration: drive 2m, count ticks
  double alpha = 0.98;         // gyro weight in the complementary filter

  long last_ticks_l = 0;
  long last_ticks_r = 0;
  bool primed = false;

  void reset(const GeoPoint& start, double heading0_deg);

  // Call at a fixed rate. gyro_rate_dps is yaw rate, degrees per second.
  // Returns distance travelled this step, in meters.
  double update(long ticks_l, long ticks_r, double gyro_rate_dps, double dt_s);

  // Snap position to a trusted GPS fix without discarding heading.
  void correct_with_gps(const GeoPoint& fix);
};
