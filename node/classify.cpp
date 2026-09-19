#include "classify.h"
#include <cstdio>
#include <cstring>

// Thresholds. Tune these on real water before the demo, then leave them alone.
static const int   RUNG_SPACING_MM   = 10;
static const int   PASSABLE_MAX_MM   = 30;   // above this, do not send a ground unit
static const int   TDS_DIRTY_PPM     = 600;  // sewage and runoff push this up
static const float SOUND_ALERT_CONF  = 0.70f;

int depth_mm(const Reading& r) {
  if (r.rungs_wet <= 0) return 0;
  return r.rungs_wet * RUNG_SPACING_MM;
}

const char* hazard_name(Hazard h) {
  switch (h) {
    case Hazard::DRY:              return "DRY";
    case Hazard::SHALLOW_CROSSING: return "SHALLOW_CROSSING";
    case Hazard::CONTAMINATED:     return "CONTAMINATED";
    case Hazard::IMPASSABLE:       return "IMPASSABLE";
    default:                       return "UNKNOWN";
  }
}

Verdict classify(const Reading& r) {
  Verdict v;
  const int mm = depth_mm(r);

  // Depth decides passability first. Contamination is a warning on top of it,
  // never a reason to call something passable.
  if (mm == 0) {
    v.hazard = Hazard::DRY;
    v.conf = 0.95f;
    snprintf(v.why, sizeof(v.why), "no rungs wet, ground is dry");
  } else if (mm > PASSABLE_MAX_MM) {
    v.hazard = Hazard::IMPASSABLE;
    v.conf = 0.90f;
    snprintf(v.why, sizeof(v.why),
             "%d of %d rungs wet, %dmm exceeds %dmm passable depth",
             r.rungs_wet, r.rungs_total, mm, PASSABLE_MAX_MM);
  } else if (r.tds_ppm >= TDS_DIRTY_PPM) {
    v.hazard = Hazard::CONTAMINATED;
    v.conf = 0.85f;
    snprintf(v.why, sizeof(v.why),
             "%dmm and turbidity %d ppm, crossable but contaminated",
             mm, r.tds_ppm);
  } else {
    v.hazard = Hazard::SHALLOW_CROSSING;
    v.conf = 0.88f;
    snprintf(v.why, sizeof(v.why),
             "%d of %d rungs wet, %dmm, turbidity %d ppm",
             r.rungs_wet, r.rungs_total, mm, r.tds_ppm);
  }

  // Audio never downgrades a hazard, it only raises urgency.
  if (r.sound_conf >= SOUND_ALERT_CONF) {
    size_t n = strlen(v.why);
    snprintf(v.why + n, sizeof(v.why) - n, " · possible distress audio");
  }
  return v;
}
