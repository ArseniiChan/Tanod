// Tanod surface unit, board 2.
//
// Reads a DFRobot SEN0628 8x8 matrix ToF looking FORWARD, decides whether the
// water ahead is blocked, and drives the hull. Prints one unit telemetry frame
// per tick to Serial, in the same line protocol ops/serial_forward.py speaks.
//
// WHAT THIS IS NOT: this unit does not navigate to a waypoint. Indoors there
// is no GNSS and no wheel odometry on a hull, so there is no position estimate,
// so "pose" is null in every frame and nav/mission.cpp is deliberately not
// used here. The decision is autonomous. The locomotion is commanded.
// Claiming otherwise would mean printing coordinates nobody measured.
//
// It does reuse nav/avoid.cpp unchanged, because that behaviour is purely
// reactive: it needs a blocked flag and nothing else. Same file the simulator
// runs, so the avoidance the judges see is the avoidance that was tested.

#include "DFRobot_MatrixLidar.h"
#include "avoid.h"
#include "gap.h"
#include <Servo.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// ===========================================================================
// DRIVE CONFIGURATION
//
//   0  servo fins only. The fins flap; thrust is sweep amplitude. Fallback for
//      when no motor or H-bridge is available.
//   1  one paddle wheel for thrust, fins held as rudders. This is the build.
//   2  two paddle wheels, true differential steering, fins unused.
//
// A 1:45 geared motor turns its output shaft near 200 RPM. That is far too
// slow for a propeller and about right for a paddle wheel, which is why this
// hull is a riverboat and not a speedboat.
// ===========================================================================
#define PADDLE_WHEELS 1

// H-bridge, TB6612FNG or L298N style: two direction pins and one PWM per
// channel. A DRV8871 takes two PWM pins instead and needs motor_write()
// rewritten; it is not wired for that here.
static const int M_L_IN1 = 4,  M_L_IN2 = 5,  M_L_PWM = 3;
static const int M_R_IN1 = 6,  M_R_IN2 = 7,  M_R_PWM = 11;

// A TB6612FNG holds both output bridges in standby until STBY is driven HIGH.
// Without this the code is right, the wiring is right, and the motor is silent,
// with nothing in telemetry to say why. Set to -1 for an L298N, which uses its
// ENA/ENB jumpers instead and has no standby pin.
static const int M_STBY = 8;

// The I2C bus the SEN0628 is on. On an UNO Q the Qwiic connector is Wire1, and
// a scan of Wire returns zero devices, so a sensor plugged into Qwiic with this
// set to Wire reports "lidar not responding" forever. Four jumpers to the
// header pins is Wire. Change this one line, do not rewire at the table.
#define TOF_BUS Wire

// Slew limit per 20 Hz tick, in units of full scale. A geared motor stepped
// from 0 to cruise off a USB power bank pulls an inrush spike that browns the
// board out and resets it mid-demo. 0.08 per tick is a 250 ms ramp to full.
// Set to 1.0f to disable ramping entirely.
static const float RAMP_PER_TICK = 0.08f;

// "drive <l> <r> [ms]" is a bench-test command, not a teleop mode. It expires
// on its own so a forgotten command cannot become a runaway.
static const long MANUAL_MAX_MS = 5000;
static const long MANUAL_DEFAULT_MS = 1500;

// The boat stops if it has not heard from the link process in this long.
// ops/link_boat.py re-sends its decision once a second for exactly this reason.
// A yanked USB cable, a sleeping host or a hard-killed link process are all
// failures the boat cannot see any other way.
static const unsigned long CMD_TIMEOUT_MS = 2000;

// Below this duty the geared motor buzzes and does not turn, which sounds like
// a fault and wastes current. Anything nonzero is lifted to it.
static const int PWM_MIN = 60;
static const int PWM_MAX = 255;

// Fins. In PADDLE_WHEELS 1 these are rudders held at an angle, not flappers.
static const int   FIN_L_PIN    = 9;
static const int   FIN_R_PIN    = 10;
static const int   FIN_CENTER   = 90;    // angle with the rudder amidships
static const int   RUDDER_MAX   = 30;    // degrees of deflection at full turn
static const int   FIN_SWEEP    = 35;    // flapping amplitude, PADDLE_WHEELS 0
static const float FIN_HZ       = 1.4f;  // flap rate, PADDLE_WHEELS 0
// Set to -1 if the two servo horns are mirrored and the rudders fight each
// other. Check this dry, before the hull is in the water.
static const int   FIN_R_SIGN   = 1;

static const int   MIN_VALID_MM = 20;    // below this the zone is noise
static const int   MAX_VALID_MM = 3500;  // sensor spec ceiling
static const int   SAFE_MM      = 600;   // a column counts as open beyond this
static const int   MIN_GAP_BINS = 2;     // an opening must fit the hull

static const float CRUISE       = 0.55f; // throttle when running clear
static const float TURN_GAIN    = 0.45f; // differential applied at full steer

static const char* SRC     = "unit-01";
static const char* MISSION = "m-live";

static const int   CTRL_HZ  = 20;        // nav/avoid.cpp counts phases in 20Hz ticks
static const int   TX_EVERY = 4;         // so telemetry goes out at 5Hz

// ---------------------------------------------------------------------------

DFRobot_MatrixLidar_I2C tof(0x33, &TOF_BUS);
Servo finL, finR;

static uint16_t zones[64];
static bool  running   = false;   // set by "go", cleared by "hold"
static bool  avoiding  = false;
static bool  sensor_ok = false;
static long  boot_epoch = 1789840000;
static long  tick = 0;

// Last time any recognised serial command arrived. Seeded in setup() so a board
// powered up with no host attached does not trip the deadman before it is told
// to do anything.
static unsigned long last_cmd_ms = 0;

// Bench-test override. manual_until is a millis() deadline; 0 means inactive.
static unsigned long manual_until = 0;
static MotorCommand  manual_cmd;

// What was actually written to the bridges on the last tick, so telemetry can
// report commanded thrust. A commanded stop and a dead driver look identical on
// a dashboard otherwise.
static float last_cmd_l = 0.0f, last_cmd_r = 0.0f;

static float clamp1(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }

// Read the whole field and choose a direction, rather than collapsing 64
// zones into one number. nav/gap.cpp does the choosing and is the same file
// the native test harness exercises, so the steering the judges watch is the
// steering that was tested. See sim/gap_test.cpp.
static Gap look_ahead() {
  Gap none;
  if (tof.getAllData(zones) != 0) {
    snprintf(none.why, sizeof(none.why), "lidar read failed");
    return none;
  }
  GapConfig cfg;
  cfg.min_valid_mm = MIN_VALID_MM;
  cfg.max_valid_mm = MAX_VALID_MM;
  cfg.safe_mm      = SAFE_MM;
  cfg.min_width    = MIN_GAP_BINS;
  return choose_gap(zones, cfg);
}

// Applied duty per channel, carried between ticks so the ramp has somewhere to
// live. Index 0 is left, 1 is right.
static float applied[2] = {0.0f, 0.0f};

// One H-bridge channel. Coast at zero rather than brake: a braked paddle wheel
// in water is a rudder nobody asked for.
//
// ch selects the ramp state. Stopping is immediate and never ramped: a slew
// limit on the way down is a slew limit on the safety path, which is the one
// place it must not exist.
static void motor_write(int ch, int in1, int in2, int pwm_pin, float cmd) {
  const float target = clamp1(cmd);

  if (fabsf(target) < 0.02f) {
    applied[ch] = 0.0f;
    digitalWrite(in1, LOW); digitalWrite(in2, LOW); analogWrite(pwm_pin, 0);
    return;
  }

  // Ramp toward the target. A sign change passes through zero on the way, so a
  // reversal never slams the bridge from full forward to full reverse.
  const float d = target - applied[ch];
  if (d > RAMP_PER_TICK)       applied[ch] += RAMP_PER_TICK;
  else if (d < -RAMP_PER_TICK) applied[ch] -= RAMP_PER_TICK;
  else                         applied[ch]  = target;

  const float out = applied[ch];
  if (fabsf(out) < 0.02f) {
    digitalWrite(in1, LOW); digitalWrite(in2, LOW); analogWrite(pwm_pin, 0);
    return;
  }

  int mag = (int)lroundf(fabsf(out) * PWM_MAX);
  if (mag < PWM_MIN) mag = PWM_MIN;
  if (mag > PWM_MAX) mag = PWM_MAX;
  digitalWrite(in1, out > 0 ? HIGH : LOW);
  digitalWrite(in2, out > 0 ? LOW  : HIGH);
  analogWrite(pwm_pin, mag);
}

static void stop_all() {
  motor_write(0, M_L_IN1, M_L_IN2, M_L_PWM, 0.0f);
  motor_write(1, M_R_IN1, M_R_IN2, M_R_PWM, 0.0f);
  finL.write(FIN_CENTER);
  finR.write(FIN_CENTER);
  last_cmd_l = 0.0f;
  last_cmd_r = 0.0f;
}

static void drive(const MotorCommand& c, float t_s) {
  const float left  = clamp1((float)c.left);
  const float right = clamp1((float)c.right);

  last_cmd_l = left;
  last_cmd_r = right;

#if PADDLE_WHEELS == 2
  (void)t_s;
  motor_write(0, M_L_IN1, M_L_IN2, M_L_PWM, left);
  motor_write(1, M_R_IN1, M_R_IN2, M_R_PWM, right);
  finL.write(FIN_CENTER);
  finR.write(FIN_CENTER);

#elif PADDLE_WHEELS == 1
  (void)t_s;
  // One wheel carries the thrust; the differential becomes rudder angle. A
  // rudder only bites while water is moving past it, so a pivot in place is
  // not available with this drive. avoid_step() reverses first, which still
  // works: the hull backs out of the obstacle before it turns.
  float thrust     = (left + right) * 0.5f;
  const float turn = clamp1((left - right) * 0.5f);

  // A pure pivot command averages to zero thrust, which on this drive means a
  // rudder at full deflection with no flow past it: 1.25 s of avoid_step()'s
  // pivot phase doing nothing at all. Give the helm some way on. This is the
  // one place the hull cannot obey the command it was given, so it obeys the
  // intent instead and keeps the sign of the turn.
  if (fabsf(thrust) < 0.05f && fabsf(turn) > 0.05f) thrust = 0.35f;

  motor_write(0, M_L_IN1, M_L_IN2, M_L_PWM, thrust);
  motor_write(1, M_R_IN1, M_R_IN2, M_R_PWM, 0.0f);
  const int defl = (int)lroundf(turn * RUDDER_MAX);
  finL.write(FIN_CENTER + defl);
  finR.write(FIN_CENTER + FIN_R_SIGN * defl);

#else
  // No motor. Fins flap: throttle becomes sweep amplitude and reverse becomes
  // a half cycle phase shift. An approximation of a thruster, not a model of
  // one, and the part most likely to need retuning in the water.
  const float phase = 2.0f * (float)M_PI * FIN_HZ * t_s;
  const float ampL  = FIN_SWEEP * fabsf(left);
  const float ampR  = FIN_SWEEP * fabsf(right);
  const float offL  = (left  < 0) ? (float)M_PI : 0.0f;
  const float offR  = (right < 0) ? (float)M_PI : 0.0f;
  finL.write(FIN_CENTER + (int)lroundf(ampL * sinf(phase + offL)));
  finR.write(FIN_CENTER + (int)lroundf(ampR * sinf(phase + offR)));
#endif
}

// "drive <left> <right> [ms]", e.g. "drive 0.6 0 1000". Parsed with strtok and
// atof rather than sscanf("%f"), because float conversion in scanf is not
// guaranteed to be linked in on every core and a silently failing parse is the
// worst possible bug in a motor test.
static void parse_drive(const String& line) {
  char buf[64];
  line.toCharArray(buf, sizeof(buf));
  strtok(buf, " \t");                          // "drive"
  const char* a = strtok(NULL, " \t");
  const char* b = strtok(NULL, " \t");
  const char* c = strtok(NULL, " \t");
  if (!a || !b) {
    Serial.println("#drive usage: drive <left> <right> [ms]");
    return;
  }
  manual_cmd.left  = clamp1((float)atof(a));
  manual_cmd.right = clamp1((float)atof(b));
  long ms = c ? atol(c) : MANUAL_DEFAULT_MS;
  if (ms < 0)              ms = 0;
  if (ms > MANUAL_MAX_MS)  ms = MANUAL_MAX_MS;
  manual_until = millis() + (unsigned long)ms;
  Serial.println("#drive ok");
}

static void handle_serial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if      (cmd == "go")   { running = true;  last_cmd_ms = millis(); }
  else if (cmd == "hold") { running = false; avoiding = false; avoid_reset();
                            manual_until = 0; stop_all(); last_cmd_ms = millis(); }
  else if (cmd == "ping") { Serial.println("#pong"); last_cmd_ms = millis(); }
  else if (cmd.startsWith("drive")) { parse_drive(cmd); last_cmd_ms = millis(); }
  else if (cmd == "stby") {
    // Report the one pin most likely to be the reason nothing moves.
    Serial.print("#stby pin ");
    Serial.println(M_STBY);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  pinMode(M_L_IN1, OUTPUT); pinMode(M_L_IN2, OUTPUT); pinMode(M_L_PWM, OUTPUT);
  pinMode(M_R_IN1, OUTPUT); pinMode(M_R_IN2, OUTPUT); pinMode(M_R_PWM, OUTPUT);

  // Take the TB6612FNG out of standby. Nothing turns until this is HIGH.
  if (M_STBY >= 0) { pinMode(M_STBY, OUTPUT); digitalWrite(M_STBY, HIGH); }

  finL.attach(FIN_L_PIN);
  finR.attach(FIN_R_PIN);

  last_cmd_ms = millis();

  // Stop first, then look for the sensor. Without this the wheel is free to
  // spin during the three seconds of lidar retry below.
  stop_all();

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

  // Deadman. ops/link_boat.py re-sends its decision once a second, so silence
  // for longer than CMD_TIMEOUT_MS means the link is gone rather than that the
  // verdict is unchanged. A boat holding its last command after the cable is
  // pulled is the one failure the rest of this file's safety argument does not
  // cover, because it is the one the boat cannot otherwise see.
  if (running && (millis() - last_cmd_ms) > CMD_TIMEOUT_MS) {
    running  = false;
    avoiding = false;
    avoid_reset();
    Serial.println("#link timeout, holding");
  }

  // Bench override, and its expiry.
  const bool manual = (manual_until != 0) && ((long)(millis() - manual_until) < 0);
  if (manual_until != 0 && !manual) { manual_until = 0; stop_all(); }

  const Gap g   = (sensor_ok && !manual) ? look_ahead() : Gap();
  const bool fault = !sensor_ok || (g.valid_bins == 0);

  MotorCommand cmd;
  const char* mode;

  if (manual) {
    // Ahead of every other branch, including the fault path, so that a motor
    // can be turned with no sensor attached. This is the only way to confirm
    // wiring, direction and PWM_MIN before the lidar works, and it expires on
    // its own so it cannot be left running by accident.
    cmd  = manual_cmd;
    mode = "MANUAL";
    avoiding = false;
  } else if (!running) {
    mode = "IDLE";
    avoiding = false;
  } else if (fault) {
    // No range means no answer, not open water. Stop rather than drive blind.
    mode = "IDLE";
    avoiding = false;
  } else if (!g.ok) {
    // The field is readable and there is nowhere to go. Back out and pivot,
    // using the same reactive behaviour the simulator runs.
    if (!avoiding) { avoid_reset(); avoiding = true; }
    cmd = avoid_step();
    if (cmd.left == 0.0 && cmd.right == 0.0) avoiding = false;
    mode = avoiding ? "AVOIDING" : "IDLE";
  } else {
    // An opening exists. Steer at its middle. This is the only place the unit
    // chooses a direction, and it does so from the sensor alone: no map, no
    // waypoint, no position estimate, because on water there is none.
    avoiding = false;
    const float turn = g.steer * TURN_GAIN;
    cmd.left  = CRUISE + turn;
    cmd.right = CRUISE - turn;
    mode = (g.steer > -0.15f && g.steer < 0.15f) ? "DRIVING" : "REROUTING";
  }

  drive(cmd, (float)millis() / 1000.0f);

  if (tick % TX_EVERY == 0) {
    char buf[448];
    const long t = boot_epoch + (long)(millis() / 1000);

    // Commanded thrust, so the console can tell a commanded stop from a dead
    // driver. Without this the two are the same picture.
    char cmd_s[40];
    snprintf(cmd_s, sizeof(cmd_s), "{\"l\":%.2f,\"r\":%.2f}",
             (double)last_cmd_l, (double)last_cmd_r);

    if (fault) {
      const char* why = manual ? "manual drive override"
                               : (sensor_ok ? g.why : "lidar not responding");
      snprintf(buf, sizeof(buf),
        "{\"src\":\"%s\",\"t\":%ld,\"pose\":null,\"mode\":\"%s\","
        "\"obstacle_mm\":null,\"steer\":null,\"fault\":\"%s\",\"cmd\":%s,\"mission\":\"%s\","
        "\"link\":{\"uplink\":true,\"inference\":\"local\"},\"decided_on\":\"device\"}",
        SRC, t, mode, why, cmd_s, MISSION);
    } else {
      // steer and gap are printed because a steering command nobody can read
      // is indistinguishable from a random one.
      char steer_s[12];
      snprintf(steer_s, sizeof(steer_s), "%.2f", (double)g.steer);
      snprintf(buf, sizeof(buf),
        "{\"src\":\"%s\",\"t\":%ld,\"pose\":null,\"mode\":\"%s\","
        "\"obstacle_mm\":%d,\"steer\":%s,\"gap\":\"%s\",\"cmd\":%s,\"mission\":\"%s\","
        "\"link\":{\"uplink\":true,\"inference\":\"local\"},\"decided_on\":\"device\"}",
        SRC, t, mode, g.ahead_mm, g.ok ? steer_s : "0.00", g.why, cmd_s, MISSION);
    }
    Serial.println(buf);
  }

  ++tick;
  delay(1000 / CTRL_HZ);
}
