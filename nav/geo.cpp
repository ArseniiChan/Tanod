#include "geo.h"
#include <cmath>

static const double R_EARTH_M = 6371008.8;
static const double DEG = M_PI / 180.0;
static const double RAD = 180.0 / M_PI;

double wrap360(double deg) {
  double d = std::fmod(deg, 360.0);
  if (d < 0) d += 360.0;
  return d;
}

double haversine_m(const GeoPoint& a, const GeoPoint& b) {
  double dlat = (b.lat - a.lat) * DEG;
  double dlon = (b.lon - a.lon) * DEG;
  double la1 = a.lat * DEG;
  double la2 = b.lat * DEG;
  double h = std::sin(dlat / 2) * std::sin(dlat / 2) +
             std::cos(la1) * std::cos(la2) *
             std::sin(dlon / 2) * std::sin(dlon / 2);
  return 2.0 * R_EARTH_M * std::asin(std::sqrt(h));
}

double bearing_deg(const GeoPoint& a, const GeoPoint& b) {
  double la1 = a.lat * DEG, la2 = b.lat * DEG;
  double dlon = (b.lon - a.lon) * DEG;
  double y = std::sin(dlon) * std::cos(la2);
  double x = std::cos(la1) * std::sin(la2) -
             std::sin(la1) * std::cos(la2) * std::cos(dlon);
  return wrap360(std::atan2(y, x) * RAD);
}

double heading_error_deg(double current_deg, double target_deg) {
  double e = std::fmod(target_deg - current_deg + 540.0, 360.0) - 180.0;
  return e;
}

GeoPoint project(const GeoPoint& from, double bearing_deg_, double dist_m) {
  double br = bearing_deg_ * DEG;
  double d = dist_m / R_EARTH_M;
  double la1 = from.lat * DEG, lo1 = from.lon * DEG;
  double la2 = std::asin(std::sin(la1) * std::cos(d) +
                         std::cos(la1) * std::sin(d) * std::cos(br));
  double lo2 = lo1 + std::atan2(std::sin(br) * std::sin(d) * std::cos(la1),
                                std::cos(d) - std::sin(la1) * std::sin(la2));
  GeoPoint p;
  p.lat = la2 * RAD;
  p.lon = wrap360((lo2 * RAD) + 180.0) - 180.0;
  return p;
}
