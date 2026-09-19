#pragma once
#include "geo.h"

enum class MissionState { IDLE, DRIVING, AVOIDING, ARRIVED, ABORTED };

struct MotorCommand {
  double left = 0;   // -1.0 .. 1.0
  double right = 0;
};

struct Mission {
  static const int MAX_WP = 16;
  GeoPoint waypoints[MAX_WP];
  int wp_count = 0;
  int wp_index = 0;

  MissionState state = MissionState::IDLE;
  double arrive_radius_m = 2.0;
  double cruise = 0.6;       // base throttle
  double max_turn = 0.35;    // cap on steering differential, retune on carpet

  void load(const GeoPoint* wps, int n);
  void start();
  void abort();

  // One control step. blocked=true routes through the avoidance behaviour.
  MotorCommand step(const GeoPoint& here, double heading_deg, bool blocked);

  const char* state_name() const;
};
