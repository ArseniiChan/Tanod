#pragma once
// Pure geodesy. No Arduino headers. Builds natively and on the ESP32.

struct GeoPoint {
  double lat;   // degrees
  double lon;   // degrees
};

// Great-circle distance in meters.
double haversine_m(const GeoPoint& a, const GeoPoint& b);

// Initial bearing from a to b, degrees clockwise from true north, [0,360).
double bearing_deg(const GeoPoint& a, const GeoPoint& b);

// Smallest signed difference target-current, in (-180,180].
// Positive means turn clockwise (right).
double heading_error_deg(double current_deg, double target_deg);

// Wrap any angle into [0,360).
double wrap360(double deg);

// Dead-reckon a new position from a start point, a heading and a distance.
GeoPoint project(const GeoPoint& from, double bearing_deg_, double dist_m);
