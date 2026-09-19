#include "deadreckon.h"
#include <cmath>

void DeadReckoner::reset(const GeoPoint& start, double heading0_deg) {
  pos = start;
  heading_deg = wrap360(heading0_deg);
  last_ticks_l = last_ticks_r = 0;
  primed = false;
}

double DeadReckoner::update(long ticks_l, long ticks_r,
                            double gyro_rate_dps, double dt_s) {
  if (!primed) {
    last_ticks_l = ticks_l;
    last_ticks_r = ticks_r;
    primed = true;
    return 0.0;
  }

  long dl = ticks_l - last_ticks_l;
  long dr = ticks_r - last_ticks_r;
  last_ticks_l = ticks_l;
  last_ticks_r = ticks_r;

  double sl = dl * m_per_tick;
  double sr = dr * m_per_tick;
  double ds = (sl + sr) * 0.5;

  // Heading change implied by differential wheel travel.
  double dtheta_odo = 0.0;
  if (track_width_m > 1e-6) {
    dtheta_odo = ((sl - sr) / track_width_m) * (180.0 / M_PI);
  }
  double dtheta_gyro = gyro_rate_dps * dt_s;

  double dtheta = alpha * dtheta_gyro + (1.0 - alpha) * dtheta_odo;
  heading_deg = wrap360(heading_deg + dtheta);

  if (std::fabs(ds) > 1e-9) {
    pos = project(pos, heading_deg, ds);
  }
  return ds;
}

void DeadReckoner::correct_with_gps(const GeoPoint& fix) {
  pos = fix;
}
