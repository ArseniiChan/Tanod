#include "avoid.h"

// Reactive obstacle behaviour. Deliberately dumb and deliberately predictable:
// back off, pivot right, run past the obstacle, then hand control back to the
// waypoint follower. Swap for VFH only once the waypoint loop is proven.
//
// Phases are in 20 Hz ticks.
static const int REVERSE_END = 15;   // 0.75 s back
static const int PIVOT_END   = 40;   // 1.25 s pivot
static const int CLEAR_END   = 75;   // 1.75 s forward, past the obstacle

static int phase_ticks = 0;

void avoid_reset() { phase_ticks = 0; }

MotorCommand avoid_step() {
  MotorCommand c;
  ++phase_ticks;
  if (phase_ticks < REVERSE_END) {
    c.left = -0.45; c.right = -0.45;
  } else if (phase_ticks < PIVOT_END) {
    c.left = 0.40;  c.right = -0.40;   // pivot right
  } else if (phase_ticks < CLEAR_END) {
    c.left = 0.50;  c.right = 0.50;    // drive clear
  } else {
    phase_ticks = 0;                   // release to waypoint following
  }
  return c;
}
