// Native test for nav/gap.cpp. No hardware, no sensor, no board.
//
//   make gaptest && ./build/gaptest
//
// Each scene is an 8x8 millimetre field laid out the way the SEN0628 fills it,
// so the same array the MCU reads is the array asserted on here. Exit code is
// the number of failures.

#include "../nav/gap.h"
#include <cstdio>
#include <cstring>

static int failures = 0;

static void show(const char* name, const uint16_t* z, const GapConfig& c, const Gap& g) {
  printf("  %-28s ok=%d steer=%+0.2f bins=[%d..%d] clear=%dmm ahead=%dmm\n"
         "  %-28s %s\n",
         name, (int)g.ok, g.steer, g.bin_lo, g.bin_hi, g.clearance_mm, g.ahead_mm,
         "", g.why);
  (void)z; (void)c;
}

static void expect(const char* name, bool cond, const char* detail) {
  if (cond) {
    printf("  PASS  %s\n", name);
  } else {
    printf("  FAIL  %s\n          %s\n", name, detail);
    ++failures;
  }
}

// Build a scene from a per-column clearance list, filled into every row.
static void scene(uint16_t* z, const int col_mm[8]) {
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x)
      z[y * 8 + x] = (uint16_t)col_mm[x];
}

int main() {
  GapConfig cfg;                 // 8x8, rows 3..4, safe 600mm, min width 2
  uint16_t z[64];
  char msg[160];

  printf("nav/gap.cpp, follow-the-gap on an 8x8 ToF field\n\n");

  // ---------------------------------------------------------------- open
  {
    const int col[8] = {2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000};
    scene(z, col);
    Gap g = choose_gap(z, cfg);
    show("wide open", z, cfg, g);
    expect("open field steers straight ahead",
           g.ok && g.steer > -0.05f && g.steer < 0.05f && g.bin_lo == 0 && g.bin_hi == 7,
           g.why);
  }

  // ------------------------------------------------- wall with a left gap
  {
    const int col[8] = {1800, 1800, 1800, 300, 300, 300, 300, 300};
    scene(z, col);
    Gap g = choose_gap(z, cfg);
    show("gap on the left", z, cfg, g);
    snprintf(msg, sizeof(msg), "steer=%+0.2f, expected clearly negative", g.steer);
    expect("an opening to port steers left", g.ok && g.steer < -0.3f, msg);
  }

  // ------------------------------------------------ wall with a right gap
  {
    const int col[8] = {300, 300, 300, 300, 300, 1800, 1800, 1800};
    scene(z, col);
    Gap g = choose_gap(z, cfg);
    show("gap on the right", z, cfg, g);
    snprintf(msg, sizeof(msg), "steer=%+0.2f, expected clearly positive", g.steer);
    expect("an opening to starboard steers right", g.ok && g.steer > 0.3f, msg);
  }

  // ------------------------------------------ doorway: narrow centre gap
  {
    const int col[8] = {200, 200, 200, 1500, 1500, 200, 200, 200};
    scene(z, col);
    Gap g = choose_gap(z, cfg);
    show("doorway ahead", z, cfg, g);
    expect("a two-bin doorway is taken, aimed at its middle",
           g.ok && g.bin_lo == 3 && g.bin_hi == 4 && g.steer > -0.05f && g.steer < 0.05f,
           g.why);
  }

  // ---------------------------- two openings, the wider one must be chosen
  {
    const int col[8] = {1500, 1500, 1500, 200, 200, 1500, 1500, 200};
    scene(z, col);
    Gap g = choose_gap(z, cfg);
    show("three left vs two right", z, cfg, g);
    expect("the wider opening wins",
           g.ok && g.bin_lo == 0 && g.bin_hi == 2, g.why);
  }

  // ------------------------- equal openings, the nearer to centre must win
  {
    const int col[8] = {1500, 1500, 200, 1500, 1500, 200, 200, 200};
    scene(z, col);
    Gap g = choose_gap(z, cfg);
    show("equal openings, tie", z, cfg, g);
    expect("a tie goes to the opening nearest ahead",
           g.ok && g.bin_lo == 3 && g.bin_hi == 4, g.why);
  }

  // ------------------------------------------------------ fully blocked
  {
    const int col[8] = {250, 250, 250, 250, 250, 250, 250, 250};
    scene(z, col);
    Gap g = choose_gap(z, cfg);
    show("wall across the field", z, cfg, g);
    expect("a wall yields no gap and no steering",
           !g.ok && g.steer == 0.0f, g.why);
  }

  // ---------------------------------- one-bin slot is refused as too narrow
  {
    const int col[8] = {200, 200, 200, 1500, 200, 200, 200, 200};
    scene(z, col);
    Gap g = choose_gap(z, cfg);
    show("slot too narrow to fit", z, cfg, g);
    expect("a one-bin slot is refused when two are required",
           !g.ok && g.bin_lo == -1, g.why);
  }

  // ------------------------------ no returns at all: fault, never "clear"
  {
    for (int i = 0; i < 64; ++i) z[i] = 0;
    Gap g = choose_gap(z, cfg);
    show("no returns anywhere", z, cfg, g);
    expect("a silent array is a fault, not open water",
           !g.ok && g.valid_bins == 0 && g.ahead_mm == -1, g.why);
  }

  // ------------- unknown columns must not be steered into, even beside a gap
  {
    const int col[8] = {0, 0, 0, 0, 1500, 1500, 0, 0};   // 0 == no return
    scene(z, col);
    Gap g = choose_gap(z, cfg);
    show("unknown beside a real gap", z, cfg, g);
    expect("unknown columns are excluded from the opening",
           g.ok && g.bin_lo == 4 && g.bin_hi == 5 && g.valid_bins == 2, g.why);
  }

  // ----------------------------------------- out-of-range values are ignored
  {
    const int col[8] = {9000, 9000, 1500, 1500, 1500, 5, 5, 9000};
    scene(z, col);
    Gap g = choose_gap(z, cfg);
    show("over and under range", z, cfg, g);
    expect("readings outside the sensor's range are discarded",
           g.ok && g.bin_lo == 2 && g.bin_hi == 4 && g.valid_bins == 3, g.why);
  }

  // ------------------------------------------------ a bad config never steers
  {
    const int col[8] = {2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000};
    scene(z, col);
    GapConfig bad = cfg;
    bad.row_hi = 99;
    Gap g = choose_gap(z, bad);
    show("row range out of bounds", z, bad, g);
    expect("a bad configuration refuses to steer rather than reading garbage",
           !g.ok && g.steer == 0.0f, g.why);
  }

  // --------------------------------------------- null pointer is not a crash
  {
    Gap g = choose_gap(nullptr, cfg);
    expect("a null field refuses to steer", !g.ok, g.why);
  }

  printf("\n%s\n", failures ? "FAILURES" : "all gap cases pass");
  return failures;
}
