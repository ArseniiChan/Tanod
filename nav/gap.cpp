#include "gap.h"
#include <cstdio>

// Per-column clearance. The outer rows of the array see the water surface and
// the ceiling, neither of which is an obstacle, so only the middle band is
// used. A column is the worst reading in that band, because the narrowest part
// of a column is what the hull actually has to fit past.
//
// Returns -1 for a column that returned nothing usable. That is UNKNOWN, and
// it is not the same as far away. A ToF zone reads nothing both when the
// target is beyond range and when the surface is too oblique or too dark to
// reflect, and the second case is a wall you are about to hit. Treating no
// return as open water is the mirror image of a corroded electrode reporting
// DRY, and it is the same class of mistake.
static int column_mm(const uint16_t* zones, const GapConfig& c, int x) {
  int worst = -1;
  for (int y = c.row_lo; y <= c.row_hi; ++y) {
    const int v = (int)zones[y * c.cols + x];
    if (v < c.min_valid_mm || v > c.max_valid_mm) continue;   // no return
    if (worst < 0 || v < worst) worst = v;
  }
  return worst;
}

Gap choose_gap(const uint16_t* zones, const GapConfig& cfg) {
  Gap g;

  if (!zones || cfg.cols <= 0 || cfg.rows <= 0 ||
      cfg.row_lo < 0 || cfg.row_hi >= cfg.rows || cfg.row_lo > cfg.row_hi ||
      cfg.min_width < 1 || cfg.min_width > cfg.cols) {
    snprintf(g.why, sizeof(g.why), "bad gap configuration, refusing to steer");
    return g;                                  // ok stays false: caller stops
  }

  // A fixed cap keeps this allocation-free on the MCU. Arrays wider than this
  // are a configuration mistake, not a runtime case.
  const int MAXC = 32;
  const int cols = cfg.cols > MAXC ? MAXC : cfg.cols;
  int clear[MAXC];

  for (int x = 0; x < cols; ++x) {
    clear[x] = column_mm(zones, cfg, x);
    if (clear[x] >= 0) ++g.valid_bins;
  }

  const int mid = cols / 2;
  g.ahead_mm = clear[mid];

  if (g.valid_bins == 0) {
    snprintf(g.why, sizeof(g.why), "no zone returned a range, sensor fault");
    return g;
  }

  // Widest run of consecutive open columns. Ties go to the run nearest centre,
  // because all else equal the cheapest turn is the one you do not make.
  int best_lo = -1, best_hi = -1, best_len = 0, best_off = 0;
  int run_lo = -1;
  for (int x = 0; x <= cols; ++x) {
    const bool open = (x < cols) && (clear[x] >= cfg.safe_mm);
    if (open && run_lo < 0) {
      run_lo = x;
    } else if (!open && run_lo >= 0) {
      const int lo = run_lo, hi = x - 1, len = hi - lo + 1;
      const int centre2 = lo + hi;                       // 2x the run centre
      const int off = centre2 - (cols - 1);              // 2x offset from ahead
      const int aoff = off < 0 ? -off : off;
      if (len > best_len || (len == best_len && aoff < best_off)) {
        best_lo = lo; best_hi = hi; best_len = len; best_off = aoff;
      }
      run_lo = -1;
    }
  }

  if (best_len < cfg.min_width) {
    if (best_len == 0)
      snprintf(g.why, sizeof(g.why), "no opening beyond %dmm in %d of %d bins",
               cfg.safe_mm, g.valid_bins, cols);
    else
      snprintf(g.why, sizeof(g.why), "widest opening is %d bins, needs %d",
               best_len, cfg.min_width);
    return g;                                   // ok stays false: caller avoids
  }

  int worst = -1;
  for (int x = best_lo; x <= best_hi; ++x)
    if (worst < 0 || clear[x] < worst) worst = clear[x];

  // Steering is the opening's centre expressed as a fraction of half the field
  // of view. Positive is right, matching the compass convention used in geo.h
  // and the MotorCommand differential in mission.h.
  const float centre = (best_lo + best_hi) * 0.5f;
  const float half   = (cols - 1) * 0.5f;
  g.ok           = true;
  g.steer        = half > 0.0f ? (centre - half) / half : 0.0f;
  g.bin_lo       = best_lo;
  g.bin_hi       = best_hi;
  g.clearance_mm = worst;

  const char* dir = g.steer < -0.15f ? "left" : (g.steer > 0.15f ? "right" : "ahead");
  snprintf(g.why, sizeof(g.why), "%d bin opening %s, %dmm clear",
           best_len, dir, worst);
  return g;
}
