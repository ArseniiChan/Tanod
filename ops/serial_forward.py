#!/usr/bin/env python3
"""
Forward one JSON telemetry line per serial line to the bridge, over TCP.

Runs on whichever laptop the board is plugged into. That does not have to be
the laptop running the dashboard, which is the whole point.

    python3 ops/serial_forward.py --host 10.189.66.227

    --list            show candidate serial ports and exit
    --port DEV        use this device instead of autodetecting
    --baud N          default 115200, must match Serial.begin() in the sketch
    --host H          bridge host, default 127.0.0.1
    --tcp N           bridge ingest port, default 8766
    --fake            emit synthetic frames instead of reading serial, so the
                      network path can be proven before the board works
    --echo            also print every forwarded line to stderr

The board is expected to print one JSON object per line, matching the contract
in docs/DASHBOARD-SPEC.md. Lines that are not JSON are reported and dropped
rather than silently forwarded, so a stray Serial.println shows up as a
complaint instead of corrupting the dashboard feed.

pyserial is used when it is installed and a plain file read is used when it is
not, so this has no hard dependencies. On macOS the /dev/cu.* device is opened
rather than /dev/tty.*, because cu does not assert DTR and so does not reset
the board every time this script starts.
"""
import argparse, glob, json, socket, subprocess, sys, time


def candidate_ports():
    """Windows has no /dev, so COM ports can only be enumerated through
    pyserial. On macOS and Linux a glob is enough and needs nothing installed."""
    if sys.platform == "win32":
        try:
            from serial.tools import list_ports
        except ImportError:
            sys.exit("On Windows this needs pyserial:\n"
                     "    py -m pip install pyserial")
        return [p.device for p in sorted(list_ports.comports())]
    pats = ["/dev/cu.usbmodem*", "/dev/cu.usbserial*", "/dev/cu.wchusbserial*",
            "/dev/cu.SLAB_USBtoUART*", "/dev/ttyACM*", "/dev/ttyUSB*"]
    out = []
    for p in pats:
        out.extend(sorted(glob.glob(p)))
    return out


def pick_port(explicit):
    if explicit:
        return explicit
    found = candidate_ports()
    if not found:
        sys.exit("no serial port found. Plug the board in, then run --list.\n"
                 "If nothing appears: the cable may be charge-only (a lot of\n"
                 "phone cables carry power but no data), or the CH340 driver\n"
                 "never attached, which is common with Elegoo and other UNO\n"
                 "clones. Try a different cable first, it is usually that.")
    if len(found) > 1:
        print(f"[fwd] several ports, using {found[0]} of {found}", file=sys.stderr)
    return found[0]


def open_serial(dev, baud):
    """pyserial when available, otherwise stty plus a plain file read."""
    try:
        import serial  # noqa
        print(f"[fwd] {dev} @ {baud} via pyserial", file=sys.stderr)
        return serial.Serial(dev, baud, timeout=1)
    except ImportError:
        pass
    if sys.platform == "win32":
        sys.exit("On Windows this needs pyserial:\n"
                 "    py -m pip install pyserial")
    flag = "-f" if sys.platform == "darwin" else "-F"
    subprocess.run(["stty", flag, dev, str(baud), "cs8", "-cstopb", "-parenb",
                    "-echo", "raw"], check=True)
    print(f"[fwd] {dev} @ {baud} via stty (pyserial not installed)", file=sys.stderr)
    return open(dev, "rb", buffering=0)


MAX_LINE = 64 * 1024   # same cap the bridge uses


def read_lines(port):
    """One decoded line at a time, from either backend. A board at the wrong
    baud rate emits bytes with no newlines, so the buffer is capped: without it
    this grows one byte of RAM per byte of garbage, forever."""
    buf = b""
    complaining = False
    while True:
        chunk = port.read(64) if hasattr(port, "read") else b""
        if not chunk:
            time.sleep(0.01)
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            yield line.decode("utf-8", "replace").strip()
        if len(buf) > MAX_LINE:
            if not complaining:
                print(f"[fwd] over {MAX_LINE} bytes with no newline. Baud rate "
                      f"probably wrong. Discarding until one arrives.",
                      file=sys.stderr)
                complaining = True
            buf = b""
        elif complaining and not buf:
            complaining = False


def fake_lines():
    """A synthetic node so the network path can be tested with no hardware."""
    t = 1789840000
    script = [(0, 120), (1, 180), (2, 300), (3, 420), (3, 680), (4, 720),
              (5, 760), (4, 700), (2, 380), (0, 140)]
    while True:
        for wet, ppm in script:
            mm = wet * 10
            state = ("DRY" if mm == 0 else "IMPASSABLE" if mm > 30
                     else "CONTAMINATED" if ppm >= 600 else "SHALLOW_CROSSING")
            yield json.dumps({
                "src": "node-02", "t": t,
                "link": {"uplink": False, "inference": "local"},
                "water": {"rungs": 6, "wet": wet, "mm": mm},
                "tds": {"ppm": ppm, "trend": "steady"},
                "temp_c": 18.0,
                "sound": {"event": "none", "conf": 0.0},
                "hazard": {"state": state, "conf": 0.85,
                           "why": f"{wet} of 6 rungs wet, {mm}mm, synthetic"},
                "decided_at": t, "decided_on": "device"})
            t += 1
            time.sleep(0.2)


def connect(host, port):
    while True:
        try:
            s = socket.create_connection((host, port), timeout=5)
            print(f"[fwd] bridge {host}:{port} connected", file=sys.stderr)
            return s
        except OSError as e:
            print(f"[fwd] bridge {host}:{port} unreachable ({e}), retry in 2s",
                  file=sys.stderr)
            time.sleep(2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--tcp", type=int, default=8766)
    ap.add_argument("--fake", action="store_true")
    ap.add_argument("--echo", action="store_true")
    a = ap.parse_args()

    if a.list:
        found = candidate_ports()
        print("\n".join(found) if found else "no serial ports found")
        return

    source = fake_lines() if a.fake else read_lines(
        open_serial(pick_port(a.port), a.baud))
    if a.fake:
        print("[fwd] --fake, no serial device opened", file=sys.stderr)

    sock = connect(a.host, a.tcp)
    sent = bad = 0
    for line in source:
        if not line:
            continue
        try:
            json.loads(line)
        except ValueError:
            bad += 1
            if bad <= 5:
                print(f"[fwd] not JSON, dropped: {line[:90]}", file=sys.stderr)
            continue
        try:
            sock.sendall((line + "\n").encode("utf-8"))
        except OSError:
            print("[fwd] bridge dropped, reconnecting", file=sys.stderr)
            sock.close()
            sock = connect(a.host, a.tcp)
            continue
        sent += 1
        if a.echo:
            print(line, file=sys.stderr)
        elif sent % 50 == 0:
            print(f"[fwd] {sent} frames forwarded, {bad} dropped", file=sys.stderr)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
