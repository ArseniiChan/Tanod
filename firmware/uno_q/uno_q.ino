// Tanod field node, Arduino UNO Q.
//
// Runs on the STM32 MCU side. Reads the water surface with a Modulino Distance
// looking DOWN at it, reads disturbance with a Modulino Movement, classifies
// the hazard, and prints one telemetry frame per reading to Serial.
//
// This sketch contains NO copy of the classifier. It compiles the real
// node/classify.cpp and node/frame.cpp, symlinked into this folder, so the
// node and the simulator cannot disagree about what counts as IMPASSABLE.
// See README.md here for the symlink step.
//
// Why a downward-looking rangefinder instead of a probe in the water:
// nothing gets wet. A contact ladder fails by corroding, and a corroded
// bottom electrode reports DRY while the road is under water, which is a
// confident false negative and the worst failure this system can have. A
// rangefinder can only fail by going silent, which is detectable.

#include "classify.h"
#include "frame.h"
#include <Modulino.h>

ModulinoDistance surface;
ModulinoMovement imu;

// ---------------------------------------------------------------------------
// CALIBRATE THIS. Measure once, in the rig you will demo with.
//
// Distance in mm from the sensor face to the DRY bottom of the tray. Water
// depth is then (MOUNT_HEIGHT_MM - measured). Get it wrong and every depth is
// wrong by the same offset, which is the easiest bug in this file to miss
// because the numbers still look plausible.
// ---------------------------------------------------------------------------
static const int MOUNT_HEIGHT_MM = 120;

// The dashboard and classifier speak in 10mm rungs, so depth is quantised to
// match. classify.cpp is untouched: it still sees a rung count, it just comes
// from a rangefinder now instead of six electrodes.
static const int RUNG_MM    = 10;
static const int RUNGS      = 6;

// Movement. Still water reads ~1g on one axis and nothing on the others, so
// disturbance is the magnitude of deviation from rest.
static const float DISTURB_FULL_G = 0.35f;   // deviation that reads as 1.0

static const char* SRC = "node-01";
static const int   HZ  = 5;

// ---------------------------------------------------------------------------
// LOCAL ALERT
//
// The obvious hole in "it decides without the network" is that a correct
// decision stranded on a microcontroller helps nobody. During the storm that
// took the towers down there is no link to send it over, and the decision is
// worth something only at the crossing, at the moment somebody is deciding
// whether to walk into the water.
//
// So the node signals where it stands. One 2 second cycle, ten 200 ms slots,
// '#' lit and '.' dark. Blink codes rather than a screen because a code is
// readable in rain, in the dark, from a distance, and costs no power budget.
//
// IMPASSABLE is deliberately the only fast pattern: the one state that must
// not be mistaken for any other reads differently from across a road.
// ---------------------------------------------------------------------------
static const int SLOT_MS   = 200;                 // matches the 5 Hz loop
static const int SLOTS     = 10;                  // so one cycle is 2 s

static const char* PAT_DRY         = "..........";
static const char* PAT_PASSABLE    = "#.........";   // one blink: go
static const char* PAT_CONTAMINATED= "#.#.......";   // two: passable, foul water
static const char* PAT_IMPASSABLE  = "#.#.#.#.#.";   // fast: do not enter
static const char* PAT_UNKNOWN     = "###.#.###.";   // long-short-long: fault

static const char* pattern_for(Hazard h) {
  switch (h) {
    case Hazard::DRY:              return PAT_DRY;
    case Hazard::SHALLOW_CROSSING: return PAT_PASSABLE;
    case Hazard::CONTAMINATED:     return PAT_CONTAMINATED;
    case Hazard::IMPASSABLE:       return PAT_IMPASSABLE;
    default:                       return PAT_UNKNOWN;
  }
}

// Called once per loop. Non-blocking: the slot comes from the clock, not from
// a delay, so the telemetry rate and the blink rate cannot drift apart.
static void signal_locally(Hazard h) {
  const int slot = (int)((millis() / SLOT_MS) % SLOTS);
  digitalWrite(LED_BUILTIN, pattern_for(h)[slot] == '#' ? HIGH : LOW);
}

static bool uplink_up       = true;
static bool inference_local = true;

static long boot_epoch = 1789840000;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  pinMode(LED_BUILTIN, OUTPUT);
  Modulino.begin();
  surface.begin();
  imu.begin();
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "uplink off")       uplink_up = false;
    else if (cmd == "uplink on")   uplink_up = true;
    else if (cmd == "infer cloud") inference_local = false;
    else if (cmd == "infer local") inference_local = true;
    else if (cmd == "cal") {
      // Calibration helper. Prints the RAW sensor reading, not a depth, so the
      // number can be copied straight into MOUNT_HEIGHT_MM. Not JSON on
      // purpose: the forwarder drops it instead of pushing it to the bridge.
      if (!surface.available()) Serial.println("#cal no reading");
      else                      Serial.print("#cal raw_mm="), Serial.println((int)surface.get());
    }
  }

  Reading r;
  r.rungs_total = RUNGS;

  // available() is false when NOTHING is in range, not when the reading is
  // zero. A sensor pointed at open air reports nothing at all, so treat an
  // unavailable sensor as a fault rather than as dry ground. This is the
  // difference between "no water" and "no answer" and they must not look alike.
  if (!surface.available()) {
    r.rungs_wet = -1;                       // classify() turns this into UNKNOWN
  } else {
    const int mm_to_surface = (int)surface.get();
    int depth_mm = MOUNT_HEIGHT_MM - mm_to_surface;
    if (depth_mm < 0) depth_mm = 0;         // surface below the tray floor is
                                            // a mounting error, not negative water
    int wet = (depth_mm + RUNG_MM / 2) / RUNG_MM;
    if (wet > RUNGS) wet = RUNGS;           // deeper than the ladder can express
    r.rungs_wet = wet;
  }

  // No turbidity sensor on this build. Reporting 0 is honest; inventing a
  // number is not. classify() treats it as clean water, which only ever makes
  // the verdict less alarming, never more.
  r.tds_ppm = 0;
  r.temp_c  = 18.0f;

  float ax, ay, az;
  imu.update();
  ax = imu.getX(); ay = imu.getY(); az = imu.getZ();
  const float mag = sqrtf(ax * ax + ay * ay + az * az);
  float disturb = fabsf(mag - 1.0f) / DISTURB_FULL_G;
  if (disturb < 0.0f) disturb = 0.0f;
  if (disturb > 1.0f) disturb = 1.0f;
  r.sound_conf = disturb;                   // water movement, not audio

  Verdict v = classify(r);                  // the real classifier, on the MCU

  // Show the verdict at the crossing before anything is transmitted anywhere.
  // This line is the only part of the system that still works when both the
  // uplink AND the laptop are gone.
  signal_locally(v.hazard);

  char buf[512];
  long t = boot_epoch + (long)(millis() / 1000);
  build_frame(buf, sizeof(buf), SRC, t, uplink_up, inference_local, r, v);
  Serial.println(buf);

  delay(1000 / HZ);
}
