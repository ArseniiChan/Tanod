#include "frame.h"
#include <cstdio>

size_t build_frame(char* out, size_t cap,
                   const char* src, long t,
                   bool uplink_up, bool inference_local,
                   const Reading& r, const Verdict& v) {
  const char* trend = r.tds_ppm >= 600 ? "rising" : "steady";
  int n = snprintf(out, cap,
    "{\"src\":\"%s\",\"t\":%ld,"
    "\"link\":{\"uplink\":%s,\"inference\":\"%s\"},"
    "\"water\":{\"rungs\":%d,\"wet\":%d,\"mm\":%d},"
    "\"tds\":{\"ppm\":%d,\"trend\":\"%s\"},"
    "\"temp_c\":%.1f,"
    "\"sound\":{\"event\":\"%s\",\"conf\":%.2f},"
    "\"hazard\":{\"state\":\"%s\",\"conf\":%.2f,\"why\":\"%s\"},"
    "\"decided_at\":%ld,\"decided_on\":\"%s\"}",
    src, t,
    uplink_up ? "true" : "false",
    inference_local ? "local" : "cloud",
    r.rungs_total, r.rungs_wet, depth_mm(r),
    r.tds_ppm, trend,
    r.temp_c,
    r.sound_conf >= 0.70f ? "distress" : "none", r.sound_conf,
    hazard_name(v.hazard), v.conf, v.why,
    t, inference_local ? "device" : "cloud");
  return n < 0 ? 0 : (size_t)n;
}
