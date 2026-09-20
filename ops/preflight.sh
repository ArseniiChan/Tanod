#!/bin/sh
# Pre-demo check. Run it 20 minutes before, and again 2 minutes before.
#
#   sh ops/preflight.sh
#
# Every line prints OK or FAIL with what to do about it. Exit code is the
# number of failures, so `sh ops/preflight.sh && echo READY` is meaningful.
#
# This exists because at 9am nobody remembers to check whether the forwarder is
# actually running, and a cached frame from a dead sensor looks exactly like a
# live frame from a steady one.

API=${API:-http://127.0.0.1:8767}
SRC=${SRC:-node-01}
FAILS=0

ok()   { printf '  OK    %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n    -> %s\n' "$1" "$2"; FAILS=$((FAILS+1)); }

echo "Tanod preflight against $API"
echo

# ---- bridge -----------------------------------------------------------------
HEALTH=$(curl -s --max-time 4 "$API/health" 2>/dev/null)
if [ -z "$HEALTH" ]; then
  bad "bridge is up" "python3 ops/ws_bridge.py --serve-only"
  echo
  echo "$FAILS failure(s). Nothing else can be checked until the bridge is up."
  exit $FAILS
fi
ok "bridge is up"

# ---- node present -----------------------------------------------------------
case "$HEALTH" in
  *"$SRC"*) ok "$SRC has been seen" ;;
  *) bad "$SRC has been seen" "python3 ops/serial_forward.py --port /dev/cu.usbmodemXXXX --host 127.0.0.1" ;;
esac

# ---- node fresh -------------------------------------------------------------
# A cached frame and a live frame are byte-identical. Age is the only tell.
AGE=$(curl -s --max-time 4 "$API/state/$SRC" 2>/dev/null \
      | python3 -c 'import json,sys
try:
    d=json.load(sys.stdin); print(d.get("age_s"))
except Exception:
    print("none")' 2>/dev/null)
case "$AGE" in
  none|"") bad "$SRC is fresh" "no frame yet; is the board plugged in and the forwarder running?" ;;
  *) if python3 -c "import sys; sys.exit(0 if float('$AGE') < 2.0 else 1)" 2>/dev/null; then
       ok "$SRC is fresh (age ${AGE}s)"
     else
       bad "$SRC is fresh (age ${AGE}s)" "forwarder is not delivering; restart serial_forward.py"
     fi ;;
esac

# ---- depth not saturated ----------------------------------------------------
# 6 of 6 rungs on every frame means the sensor is not mounted at its calibrated
# height, or MOUNT_HEIGHT_MM is still the placeholder. Either way the depth is
# wrong and the water demo has no transition to show.
SAT=$(curl -s --max-time 4 "$API/state/$SRC" 2>/dev/null \
      | python3 -c 'import json,sys
try:
    w=json.load(sys.stdin)["frame"]["water"]; print(1 if w["wet"]>=w["rungs"] else 0)
except Exception:
    print("?")' 2>/dev/null)
case "$SAT" in
  0) ok "depth is not saturated" ;;
  1) bad "depth is not saturated" "reads full scale on every frame: calibrate MOUNT_HEIGHT_MM (send 'cal' on the node's serial port)" ;;
  *) bad "depth is not saturated" "could not read water block from the frame" ;;
esac

# ---- uplink not left cut ----------------------------------------------------
UP=$(curl -s --max-time 4 "$API/uplink" 2>/dev/null)
case "$UP" in
  *'"blocked_at_bridge": true'*) bad "uplink is restored" "CUT NETWORK is still latched from the last run; press it again" ;;
  *) ok "uplink is restored" ;;
esac

# ---- cloud path answers -----------------------------------------------------
# source:"probe" means the key is not in the BRIDGE's environment. Exporting it
# in another shell does nothing; the bridge has to be started from that shell.
# Exactly the body the dashboard sends: name the source and let the bridge use
# the frame it last saw. Posting a bare frame also "works", but only because a
# body with no "frame" key falls through to src="node-01", which is an accident
# rather than the contract.
TRI=$(curl -s --max-time 10 -X POST -H "Content-Type: application/json" \
      -d "{\"src\":\"$SRC\"}" "$API/triage" 2>/dev/null)
case "$TRI" in
  *'"source": "probe"'*) bad "cloud triage answers" "OPENAI_API_KEY is not in the bridge's environment; export it, then restart the bridge from that shell" ;;
  *'"decided_on": "cloud"'*) ok "cloud triage answers" ;;
  *) bad "cloud triage answers" "no cloud answer: $(printf '%s' "$TRI" | cut -c1-90)" ;;
esac

# ---- spec served ------------------------------------------------------------
if curl -s --max-time 4 -o /dev/null -w '%{http_code}' "$API/openapi.yaml" 2>/dev/null | grep -q 200; then
  ok "openapi.yaml is served"
else
  bad "openapi.yaml is served" "run the bridge from the repository root"
fi

echo
if [ "$FAILS" -eq 0 ]; then
  echo "READY. Open $API/?src=live and keep that tab in the foreground."
else
  echo "$FAILS failure(s). Fix these before the judges arrive."
fi
exit $FAILS
