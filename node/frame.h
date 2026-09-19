#pragma once
#include "classify.h"
#include <cstddef>

// Builds the exact JSON frame the dashboard expects. One place, one shape,
// so firmware and harness can never drift apart.
//
// decided_on is "device" or "cloud". The dashboard renders it as a badge.
// That single field is the whole thesis, so it is never hardcoded true.
size_t build_frame(char* out, size_t cap,
                   const char* src,
                   long t,
                   bool uplink_up,
                   bool inference_local,
                   const Reading& r,
                   const Verdict& v);
