#!/usr/bin/env python3
"""Turn the node's verdict into a command the boat obeys.

This is the wire that makes the offline claim physical. The node decides
IMPASSABLE on its own MCU with the uplink cut; this reads that verdict off the
bridge and writes "hold" down the boat's serial port. Without this file the
claim is an animation on a dashboard.

    python3 ops/link_boat.py --port /dev/cu.usbmodemXXXX

Flags:
    --port      boat serial port. --list to see candidates.
    --api       bridge HTTP base, default http://127.0.0.1:8767
    --src       node to follow, default node-01
    --interval  seconds between polls, default 0.5
    --dry-run   decide and log, write nothing to the port

Stdlib only, same as the rest of ops/. No pyserial, no requests.

Open the port here BEFORE starting ops/serial_forward.py on the same port.
Opening a USB CDC port toggles DTR and resets the board, so you want that
reset to happen at startup rather than in the middle of a demo.
"""

import argparse
import glob
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

# Anything that is not a confident "you may proceed" stops the boat. UNKNOWN is
# a sensor fault, and a fault must read as stop, never as clear. This is the
# whole safety argument of the project expressed as one tuple.
GO_STATES = ("DRY", "PASSABLE")

# Older than this and the node is not talking, which is also a reason to stop.
STALE_AFTER_S = 10.0

# Re-send the current decision at least this often, even when unchanged. The
# boat stops if it hears nothing for CMD_TIMEOUT_MS (2000 ms in
# firmware/boat/boat.ino), so this has to stay well under that. Raise one and
# you must raise the other.
HEARTBEAT_S = 1.0


def list_ports():
    return sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*")
                  + glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))


def open_port(path, baud):
    """Raw, no echo, no hangup on close. Returns a writable fd."""
    if sys.platform != "win32":
        stty = ["stty", "-f", path] if sys.platform == "darwin" else ["stty", "-F", path]
        subprocess.run(stty + [str(baud), "raw", "-echo", "-hupcl"],
                       check=False, capture_output=True)
    return os.open(path, os.O_RDWR | os.O_NOCTTY)


def poll(api, src, timeout):
    """(state, stale) from the bridge. state is None when it cannot be read."""
    url = f"{api.rstrip('/')}/state/{src}"
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            doc = json.loads(r.read().decode("utf-8"))
    except (urllib.error.URLError, OSError, ValueError, json.JSONDecodeError) as e:
        return None, True, f"bridge unreachable: {e}"

    if not isinstance(doc, dict) or "frame" not in doc:
        return None, True, "no frame for this source yet"

    frame = doc.get("frame") or {}
    hazard = frame.get("hazard") or {}
    state = hazard.get("state")
    age = doc.get("age_s")
    stale = bool(doc.get("stale")) or (isinstance(age, (int, float)) and age > STALE_AFTER_S)
    return state, stale, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--api", default="http://127.0.0.1:8767")
    ap.add_argument("--src", default="node-01")
    ap.add_argument("--interval", type=float, default=0.5)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if args.list:
        found = list_ports()
        print("\n".join(found) if found else "no serial ports found")
        return 0

    fd = None
    if not args.dry_run:
        if not args.port:
            print("[link] --port is required (or --dry-run, or --list)", file=sys.stderr)
            return 2
        try:
            fd = open_port(args.port, args.baud)
        except OSError as e:
            print(f"[link] cannot open {args.port}: {e}", file=sys.stderr)
            return 1
        print(f"[link] holding {args.port} open at {args.baud}", file=sys.stderr)

    print(f"[link] following {args.src} on {args.api}", file=sys.stderr)

    # Start from neither, so the first decision is always transmitted.
    #
    # The decision is also re-sent every HEARTBEAT_S even when it has not
    # changed, because the boat runs a deadman: silence for longer than its
    # CMD_TIMEOUT_MS is read as a dead link and stops the motors. Edge-triggered
    # writes alone would trip that within seconds of a steady "go".
    #
    # Logging stays edge-triggered, so the serial log is still readable during
    # a demo. The wire is chatty; the console is not.
    #
    # These two numbers are a pair. HEARTBEAT_S must stay comfortably below the
    # boat's CMD_TIMEOUT_MS, and neither should be changed without the other.
    last = None
    last_tx = 0.0
    try:
        while True:
            state, stale, err = poll(args.api, args.src, timeout=max(1.0, args.interval * 2))
            want = "go" if (state in GO_STATES and not stale) else "hold"

            now = time.monotonic()
            changed = want != last
            due = (now - last_tx) >= HEARTBEAT_S

            if changed or due:
                if changed:
                    why = err or (f"{args.src} stale" if stale else f"{args.src} is {state}")
                    print(f"[link] {want.upper():4s}  ({why})", file=sys.stderr)
                if fd is not None:
                    try:
                        os.write(fd, (want + "\n").encode("ascii"))
                    except OSError as e:
                        # A boat that unplugs mid-run must not take the link
                        # process down with it. Log, drop the fd, keep polling,
                        # and let the operator see it in one place.
                        print(f"[link] write failed, port gone: {e}", file=sys.stderr)
                        try:
                            os.close(fd)
                        except OSError:
                            pass
                        fd = None
                        last = None
                        time.sleep(args.interval)
                        continue
                last = want
                last_tx = now

            time.sleep(args.interval)
    except KeyboardInterrupt:
        pass
    finally:
        # Leave the boat stopped. A link process that exits while the fins are
        # still flapping is a boat nobody is commanding.
        if fd is not None:
            try:
                os.write(fd, b"hold\n")
                os.close(fd)
            except OSError:
                pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
