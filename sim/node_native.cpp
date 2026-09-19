// Laptop build of the node. Runs the EXACT classifier and frame builder the
// ESP32 will run, against a scripted flood, and streams frames to stdout.
//
//   ./build/node                 loop forever at 5 Hz   (dashboard feed)
//   ./build/node --once          one pass, then exit    (tests, piping to a file)
//   ./build/node --rate 20       faster
//   ./build/node --hold 6        freeze on script step 6 (IMPASSABLE) and hold
//
// The dashboard can be finished before a single board is in our hands.
#include "../node/classify.h"
#include "../node/frame.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <thread>

struct Step { int wet; int tds; float temp; float sound; int hold; };

// A scripted flood: dry, water rises past passable, turbidity climbs, a
// distress sound, then it recedes. Every state the dashboard must render.
static const Step SCRIPT[] = {
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
static const int N_STEPS = sizeof(SCRIPT) / sizeof(SCRIPT[0]);

// The uplink dies partway through the first pass and never comes back. Frames
// keep coming anyway, because nothing here ever needed it.
static const int UPLINK_DIES_AT = 40;

static void emit(const Step& s, long t, bool uplink) {
  char buf[512];
  Reading r;
  r.rungs_wet  = s.wet;
  r.tds_ppm    = s.tds;
  r.temp_c     = s.temp;
  r.sound_conf = s.sound;
  Verdict v = classify(r);
  build_frame(buf, sizeof(buf), "node-01", t, uplink, /*local=*/true, r, v);
  printf("%s\n", buf);
  fflush(stdout);
}

int main(int argc, char** argv) {
  bool once = false;
  double rate = 5.0;
  int hold_step = -1;

  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--once")) once = true;
    else if (!strcmp(argv[i], "--rate") && i + 1 < argc) rate = atof(argv[++i]);
    else if (!strcmp(argv[i], "--hold") && i + 1 < argc) hold_step = atoi(argv[++i]);
    else {
      fprintf(stderr,
        "usage: %s [--once] [--rate HZ] [--hold STEP 0..%d]\n", argv[0], N_STEPS - 1);
      return 2;
    }
  }
  if (rate <= 0) rate = 5.0;
  const auto period = std::chrono::microseconds((long)(1e6 / rate));

  long t = 1789840000;
  int tick = 0;

  // Freeze on one step so the demo can be rehearsed against a fixed state.
  if (hold_step >= 0 && hold_step < N_STEPS) {
    for (;;) {
      emit(SCRIPT[hold_step], t++, tick++ < UPLINK_DIES_AT);
      std::this_thread::sleep_for(period);
    }
  }

  do {
    for (int s = 0; s < N_STEPS; ++s) {
      for (int h = 0; h < SCRIPT[s].hold; ++h) {
        emit(SCRIPT[s], t++, tick < UPLINK_DIES_AT);
        ++tick;
        std::this_thread::sleep_for(period);
      }
    }
  } while (!once);

  return 0;
}
