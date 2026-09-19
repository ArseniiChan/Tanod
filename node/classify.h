#pragma once
// Hazard classification. Pure C++, no Arduino headers, so it builds natively
// for the test harness AND into the ESP32 firmware unchanged. Same rule as nav/.

enum class Hazard { UNKNOWN, DRY, SHALLOW_CROSSING, CONTAMINATED, IMPASSABLE };

struct Reading {
  int   rungs_total = 6;
  int   rungs_wet   = 0;      // from the printed ladder, GPIO per rung
  int   tds_ppm     = 0;      // Gravity analog TDS
  float temp_c      = 0.0f;
  float sound_conf  = 0.0f;   // distress-audio confidence, 0 if unused
};

struct Verdict {
  Hazard hazard = Hazard::UNKNOWN;
  float  conf   = 0.0f;
  char   why[120] = {0};      // plain sentence for the dashboard, never a code
};

// Depth in mm implied by how many rungs are wet. Rungs sit every 10mm.
int depth_mm(const Reading& r);

// The whole decision. Runs on the chip. Nothing here touches the network.
Verdict classify(const Reading& r);

const char* hazard_name(Hazard h);
