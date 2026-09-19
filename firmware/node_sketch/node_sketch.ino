// Tikbalang field node. Reads a water ladder, classifies on the chip, prints
// one telemetry frame per reading to Serial.
//
// This sketch does NOT contain a copy of the classifier. It compiles the real
// node/classify.cpp and node/frame.cpp, symlinked into this folder, so the
// board and the simulator can never disagree about what counts as IMPASSABLE.
// See README.md in this folder for the symlink step.
//
// Wiring, per rung:
//
//     3V3 ---[ probe A ]~~ water ~~[ probe B ]--- GPIO --- 10k --- GND
//
// The 10k to ground is what makes this readable. Without it the pin floats and
// reports whatever it feels like. Water bridging the probes pulls the pin up.

#include "classify.h"
#include "frame.h"

// ADC1 only. ADC2 shares hardware with the WiFi radio and stops returning
// usable values the moment WiFi is on, which is a miserable thing to debug.
static const int RUNG_PIN[] = {1, 2, 3, 4, 5, 6};
static const int N_RUNGS    = sizeof(RUNG_PIN) / sizeof(RUNG_PIN[0]);
static const int TDS_PIN    = 7;      // -1 if no TDS sensor is fitted
static const int SOUND_PIN  = -1;     // -1 if no sound sensor is fitted

// Above this raw ADC value a rung counts as wet. Calibrate it: run with
// CALIBRATE set to 1, read the numbers dry, then wet, and pick the midpoint.
// 12-bit ADC, so the range is 0 to 4095.
static int   wet_threshold = 1800;
static const int CALIBRATE = 0;

static const char* SRC  = "node-01";
static const int   HZ   = 5;

// Flipped by a command on Serial, never hardcoded. The whole claim of this
// project is that this field is answered honestly.
static bool uplink_up       = true;
static bool inference_local = true;

static long boot_epoch = 1789840000;

static int read_avg(int pin) {
  long sum = 0;
  for (int i = 0; i < 8; ++i) sum += analogRead(pin);   // 8 samples kills the
  return (int)(sum / 8);                                // worst of the noise
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  analogReadResolution(12);
  for (int i = 0; i < N_RUNGS; ++i) pinMode(RUNG_PIN[i], INPUT);
  if (TDS_PIN >= 0) pinMode(TDS_PIN, INPUT);
  if (SOUND_PIN >= 0) pinMode(SOUND_PIN, INPUT);
}

void loop() {
  // "uplink off" / "uplink on" over Serial, so the cut can be driven from the
  // dashboard later without reflashing.
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "uplink off") uplink_up = false;
    else if (cmd == "uplink on") uplink_up = true;
  }

  if (CALIBRATE) {
    Serial.print("raw:");
    for (int i = 0; i < N_RUNGS; ++i) {
      Serial.print(' ');
      Serial.print(read_avg(RUNG_PIN[i]));
    }
    if (TDS_PIN >= 0) { Serial.print("  tds:"); Serial.print(read_avg(TDS_PIN)); }
    Serial.println();
    delay(1000 / HZ);
    return;
  }

  Reading r;
  r.rungs_total = N_RUNGS;

  // Count every wet rung, then check the pattern is physically possible.
  // Stopping at the first dry rung looks tidy but converts a single broken
  // electrode into a confident DRY reading while the road is under water, and
  // the bottom rung is the one that corrodes first. A dry rung below a wet one
  // cannot happen with standing water, so it is a fault: report -1 and let the
  // classifier say UNKNOWN rather than inventing a depth.
  int wet = 0;
  bool seen_dry = false, fault = false;
  for (int i = 0; i < N_RUNGS; ++i) {
    const bool is_wet = read_avg(RUNG_PIN[i]) >= wet_threshold;
    if (is_wet) {
      ++wet;
      if (seen_dry) fault = true;      // wet rung above a dry one
    } else {
      seen_dry = true;
    }
  }
  r.rungs_wet = fault ? -1 : wet;

  // Gravity analog TDS, roughly 0 to 1000 ppm across the ADC range. Replace
  // with the vendor curve once there is a reference solution to check against.
  r.tds_ppm = (TDS_PIN >= 0) ? (int)(read_avg(TDS_PIN) * 1000L / 4095L) : 0;

  r.temp_c = 18.0f;                     // DS18B20 goes here when one is fitted
  r.sound_conf = (SOUND_PIN >= 0)
      ? (float)read_avg(SOUND_PIN) / 4095.0f : 0.0f;

  Verdict v = classify(r);               // the real classifier, on the chip

  char buf[512];
  long t = boot_epoch + (long)(millis() / 1000);
  build_frame(buf, sizeof(buf), SRC, t, uplink_up, inference_local, r, v);
  Serial.println(buf);

  delay(1000 / HZ);
}
