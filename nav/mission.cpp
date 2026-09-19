#include "mission.h"
#include "avoid.h"
#include <algorithm>
#include <cmath>

static double clamp1(double v) { return std::max(-1.0, std::min(1.0, v)); }

void Mission::load(const GeoPoint* wps, int n) {
  wp_count = std::min(n, MAX_WP);
  for (int i = 0; i < wp_count; ++i) waypoints[i] = wps[i];
  wp_index = 0;
  state = MissionState::IDLE;
}

void Mission::start() {
  if (wp_count > 0) state = MissionState::DRIVING;
}

void Mission::abort() { state = MissionState::ABORTED; }

MotorCommand Mission::step(const GeoPoint& here, double heading_deg, bool blocked) {
  MotorCommand cmd;
  if (state != MissionState::DRIVING && state != MissionState::AVOIDING) {
    return cmd;
  }

  if (blocked) {
    state = MissionState::AVOIDING;
    return avoid_step();
  }
  state = MissionState::DRIVING;

  const GeoPoint& target = waypoints[wp_index];
  if (haversine_m(here, target) <= arrive_radius_m) {
    if (wp_index + 1 < wp_count) {
      ++wp_index;
    } else {
      state = MissionState::ARRIVED;
      return cmd;
    }
  }

  double want = bearing_deg(here, target);
  double err  = heading_error_deg(heading_deg, want);   // + means turn right

  // Turn authority is capped. A 0.15m track width gives enormous yaw rates at
  // full differential, so full-scale steering makes the rover oscillate instead
  // of converge. max_turn is the knob to retune on real carpet.
  double turn = clamp1(err / 90.0) * max_turn;

  // Cruise falls off as cos(error) and hits zero at 90 degrees, so a badly
  // misaligned rover pivots in place rather than arcing wide.
  double a = std::fabs(err) > 90.0 ? 90.0 : std::fabs(err);
  double base = cruise * std::cos(a * M_PI / 180.0);

  cmd.left  = clamp1(base + turn);
  cmd.right = clamp1(base - turn);
  return cmd;
}

const char* Mission::state_name() const {
  switch (state) {
    case MissionState::IDLE: return "IDLE";
    case MissionState::DRIVING: return "DRIVING";
    case MissionState::AVOIDING: return "AVOIDING";
    case MissionState::ARRIVED: return "ARRIVED";
    case MissionState::ABORTED: return "ABORTED";
  }
  return "?";
}
