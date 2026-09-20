// Tikbalang surface unit, board 2.
//
// Reads a DFRobot SEN0628 8x8 matrix ToF looking FORWARD, decides whether the
// water ahead is blocked, and drives two MG996R servos as flapping fins.
// Prints one unit telemetry frame per tick to Serial, in the same line
// protocol ops/serial_forward.py already speaks.
//
// WHAT THIS IS NOT: this unit does not navigate to a waypoint. Indoors there
// is no GPS and no wheel odometry on a boat, so there is no position estimate,
// so "pose" is null in every frame and nav/mission.cpp is deliberately not
// used here. The decision is autonomous. The locomotion is commanded.
// Claiming otherwise would mean printing coordinates nobody measured.
//
// It does reuse nav/avoid.cpp unchanged, because that behaviour is purely
// reactive: it needs a blocked flag and nothing else. Same file the simulator
// runs, so the avoidance the judges see is the avoidance that was tested.
//
// Wiring:
//   SEN0628   Gravity 4-pin I2C, address 0x33. The UNO Q Qwiic connector is
//             JST-SH 1mm and Gravity is JST-PH 2mm, so this needs an adapter
//             or four jumpers: VCC 3V3, GND, SDA, SCL.
//   Servos    signal on FIN_L_PIN / FIN_R_PIN. Power them from the Anker,
//             NOT from the board: an MG996R stalls at about 2.5A and will
//             brown out the MCU mid-demo. Tie the battery ground to board
//             ground or the servos will jitter.

#include "DFRobot_MatrixLidar.h"
#include "avoid.h"
#include <Servo.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Tunables. Every one of these is a guess until it is measured in the tub.
// ---------------------------------------------------------------------------
static const int   FIN_L_PIN    = 9;
static const int   FIN_R_PIN    = 10;

static const int   FIN_CENTER   = 90;    // servo angle with the fin straight back
static const int   FIN_SWEEP    = 35;    // degrees either side at full throttle
static const float FIN_HZ       = 1.4f;  // flap rate

static const int   BLOCK_MM     = 400;   // closer than this counts as blocked
static const int   MIN_VALID_MM = 20;    // below this the zone is noise, not a target
static const int   MAX_VALID_MM = 3500;  // sensor spec ceiling

static const float CRUISE       = 0.55f; // throttle when running clear

static const char* SRC     = "unit-01";
static const char* MISSION = "m-live";

static const int   CTRL_HZ = 20;         // nav/avoid.cpp counts phases in 20Hz ticks
static const int   TX_EVERY = 4;         // so telemetry goes out at 5Hz

// ---------------------------------------------------------------------------

DFRobot_MatrixLidar_I2C tof(0x33, &Wire);
Servo finL, finR;

static uint16_t zones[64];
static bool  running   = false;   // set by "go", cleared by "hold"
static bool  avoiding  = false;
static bool  sensor_ok = false;
static long  boot_epoch = 1789840000;
static long  tick = 0;

// Minimum range across the forward-facing centre of the array. The outer rows
// see the water surface and the ceiling, which are not obstacles, so only the
// middle band is used. Returns -1 when no zone returned a usable range, which
// is a fault and must not be read as "clear".
static int forward_mm() {
  if (tof.getAllData(zones) != 0) return -1;
  int best = -1;
  for (int y = 3; y <= 4; ++y) {
    for (int x = 2; x <= 5; ++x) {
      const int v = (int)zones[y * 8 + x];
      if (v < MIN_VALID_MM || v > MAX_VALID_MM) continue;   // no return
      if (best < 0 || v < best) best = v;
    }
  }
  return best;
}

// Map a differential drive command onto two flapping fins. A fin makes thrust
// by sweeping, so throttle becomes sweep amplitude and reverse becomes a half
// cycle phase shift. This is an approximation of a thruster, not a model of
// one, and it is the part most likely to need retuning in the water.
static void drive(const MotorCommand& c, float t_s) {
  const float phase = 2.0f * (float)M_PI * FIN_HZ * t_s;
  const float ampL  = FIN_SWEEP * fminf(1.0f, fabsf((float)c.left));
  const float ampR  = FIN_SWEEP * fminf(1.0f, fabsf((float)c.right));
  const float offL  = (c.left  < 0) ? (float)M_PI : 0.0f;
  const float offR  = (c.right < 0) ? (float)M_PI : 0.0f;
  finL.write(FIN_CENTER + (int)lroundf(ampL * sinf(phase + offL)));
  finR.write(FIN_CENTER + (int)lroundf(ampR * sinf(phase + offR)));
}

static void handle_serial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if      (cmd == "go")   running = true;
  else if (cmd == "hold") { running = false; avoiding = false; avoid_reset(); }
  else if (cmd == "ping") Serial.println("#pong");
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  finL.attach(FIN_L_PIN);
  finR.attach(FIN_R_PIN);
  finL.write(FIN_CENTER);
  finR.write(FIN_CENTER);

  // Do not spin forever on a missing sensor the way the DFRobot example does.
  // A unit that never reaches loop() also never reports that it is broken,
  // and a silent board looks identical to an unplugged one.
  for (int i = 0; i < 10 && !sensor_ok; ++i) {
    if (tof.begin() == 0) sensor_ok = true; else delay(300);
  }
  if (sensor_ok && tof.setRangingMode(eMatrix_8X8) != 0) sensor_ok = false;
}

void loop() {
  handle_serial();

  const int mm = sensor_ok ? forward_mm() : -1;
  const bool fault   = (mm < 0);
  const bool blocked = (!fault && mm < BLOCK_MM);

  MotorCommand cmd;
  const char* mode;

  if (!running) {
    mode = "IDLE";
    avoiding = false;
  } else if (fault) {
    // No range means no answer, not open water. Stop rather than drive blind.
    mode = "IDLE";
    avoiding = false;
  } else {
    if (blocked && !avoiding) { avoid_reset(); avoiding = true; }
    if (avoiding) {
      cmd = avoid_step();
      // avoid.cpp returns a zero command on the tick its cycle completes.
      if (cmd.left == 0.0 && cmd.right == 0.0) { avoiding = false; }
    }
    if (avoiding) {
      mode = "AVOIDING";
    } else {
      cmd.left = CRUISE; cmd.right = CRUISE;
      mode = "DRIVING";
    }
  }

  drive(cmd, (float)millis() / 1000.0f);

  if (tick % TX_EVERY == 0) {
    char buf[256];
    const long t = boot_epoch + (long)(millis() / 1000);
    if (fault) {
      snprintf(buf, sizeof(buf),
        "{\"src\":\"%s\",\"t\":%ld,\"pose\":null,\"mode\":\"%s\","
        "\"obstacle_mm\":null,\"fault\":\"%s\",\"mission\":\"%s\","
        "\"link\":{\"uplink\":true,\"inference\":\"local\"},\"decided_on\":\"device\"}",
        SRC, t, mode, sensor_ok ? "no range returned" : "lidar not responding", MISSION);
    } else {
      snprintf(buf, sizeof(buf),
        "{\"src\":\"%s\",\"t\":%ld,\"pose\":null,\"mode\":\"%s\","
        "\"obstacle_mm\":%d,\"mission\":\"%s\","
        "\"link\":{\"uplink\":true,\"inference\":\"local\"},\"decided_on\":\"device\"}",
        SRC, t, mode, mm, MISSION);
    }
    Serial.println(buf);
  }

  ++tick;
  delay(1000 / CTRL_HZ);
}
