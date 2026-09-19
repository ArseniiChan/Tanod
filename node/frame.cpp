#include "frame.h"
#include <cstdio>

size_t build_frame(char* out, size_t cap,
                   const char* src, long t,
                   bool uplink_up, bool inference_local,
                   const Reading& r, const Verdict& v) {
  // A trend needs memory of the last reading. The old version tested
  // ppm >= 600, which reported "rising" while the water was receding: script
  // step 8 drops 760 -> 700 and still printed "rising". The 20 ppm deadband
  // keeps sensor noise from flapping the label every frame.
  static int prev_ppm = -1;
  const char* trend = "steady";
  if (prev_ppm >= 0) {
    if      (r.tds_ppm > prev_ppm + 20) trend = "rising";
    else if (r.tds_ppm < prev_ppm - 20) trend = "falling";
  }
  prev_ppm = r.tds_ppm;
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
