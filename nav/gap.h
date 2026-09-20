#pragma once
#include <cstdint>

// Reactive path choice from a time-of-flight array. Pure C++, no Arduino
// headers, so it builds natively for the test harness and onto the MCU
// unchanged. Same rule as node/classify.cpp and the rest of nav/.
//
// WHY THIS EXISTS. A matrix rangefinder collapsed to its minimum reading is a
// bump sensor with extra steps: it can say "something is there" and nothing
// else. The array is 8 columns across a 60 degree field, which is 8 angular
// bins, which is enough to answer a better question: not "is the way blocked"
// but "which way is open". That is a steering command, and it needs no map, no
// GPS and no position estimate, which matters because on water there is none
// of the three.
//
// This is follow-the-gap, the simplest member of the vector-field-histogram
// family. Widest opening wins, aim at its middle. It is deliberately dumb and
// deliberately predictable: it cannot plan around a wall, and it will oscillate
// between two equal gaps unless hysteresis is applied upstream. Both are honest
// limits of a purely reactive method, not bugs.

struct Gap {
  bool  ok            = false;  // a gap wide enough to pass was found
  float steer         = 0.0f;   // -1 hard left, 0 straight, +1 hard right
  int   bin_lo        = -1;     // inclusive column index of the chosen opening
  int   bin_hi        = -1;     // inclusive
  int   clearance_mm  = 0;      // worst clearance inside the chosen opening
  int   ahead_mm      = -1;     // clearance dead ahead, -1 if unknown
  int   valid_bins    = 0;      // columns that returned a usable range
  char  why[96]       = {0};    // plain sentence, never a code
};

struct GapConfig {
  int cols        = 8;     // columns in the array
  int rows        = 8;     // rows in the array
  int row_lo      = 3;     // first row to use, inclusive
  int row_hi      = 4;     // last row to use, inclusive
  int min_valid_mm= 20;    // below this a reading is noise, not a target
  int max_valid_mm= 3500;  // sensor ceiling
  int safe_mm     = 600;   // a column counts as open at or beyond this
  int min_width   = 2;     // an opening must be this many columns wide to fit
};

// zones is row-major, zones[y * cfg.cols + x], in millimetres, as the
// DFRobot SEN0628 getAllData() fills it. A zone of 0 or out of range means no
// return, which is treated as UNKNOWN and never as open: an unknown column
// cannot be steered into. That is the same rule the hazard classifier uses,
// for the same reason.
Gap choose_gap(const uint16_t* zones, const GapConfig& cfg);
