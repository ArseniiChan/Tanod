// Laptop build of the node. Runs the EXACT classifier and frame builder the
// ESP32 will run, against a scripted flood, and prints frames to stdout at 5 Hz.
//
//   make node && ./build/node
//   ./build/node | nc -l 8081        # or pipe straight into the dashboard mock
//
// The dashboard can be finished before a single board is in our hands.
#include "../node/classify.h"
#include "../node/frame.h"
#include <cstdio>
#include <cstring>
#include <cmath>

int main() {
  char buf[512];
  long t = 1789840000;

  // A scripted flood: dry, water rises past passable, turbidity climbs,
  // a distress sound, then it recedes. Every state the dashboard must render.
  struct Step { int wet; int tds; float temp; float sound; int hold; };
  const Step script[] = {
    {0, 120, 19.0f, 0.00f, 12},
    {1, 180, 18.6f, 0.00f,  8},
    {2, 300, 18.2f, 0.00f,  8},
    {3, 420, 17.9f, 0.00f, 10},
    {3, 680, 17.6f, 0.00f, 10},   // turbidity crosses -> CONTAMINATED
    {4, 720, 17.4f, 0.81f,  8},   // deeper + distress audio
    {5, 760, 17.1f, 0.84f, 10},   // IMPASSABLE
    {4, 700, 17.3f, 0.40f,  8},
    {2, 380, 17.8f, 0.00f,  8},
    {0, 140, 18.4f, 0.00f,  6},
  };

  // The uplink dies partway through and never comes back. The frames keep
  // coming anyway, because nothing here needed it.
  const int UPLINK_DIES_AT = 40;
  int tick = 0;

  for (size_t s = 0; s < sizeof(script)/sizeof(script[0]); ++s) {
    for (int h = 0; h < script[s].hold; ++h, ++tick) {
      Reading r;
      r.rungs_wet  = script[s].wet;
      r.tds_ppm    = script[s].tds;
      r.temp_c     = script[s].temp;
      r.sound_conf = script[s].sound;

      Verdict v = classify(r);

      bool uplink = tick < UPLINK_DIES_AT;
      build_frame(buf, sizeof(buf), "node-01", t, uplink, /*local=*/true, r, v);
      printf("%s\n", buf);
      fflush(stdout);
      t += 1;
    }
  }
  return 0;
}
