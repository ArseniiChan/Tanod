#pragma once
#include "mission.h"

// Reactive obstacle behaviour. Deliberately dumb and deliberately predictable:
// back off, then pivot right, then hand control back to the waypoint follower.
// Swap for VFH once the waypoint loop is proven.
MotorCommand avoid_step();
void avoid_reset();
