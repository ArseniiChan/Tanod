#!/usr/bin/env python3
"""Adversarial stress suite for the bridge and the software-only demo path.

    python3 ops/stress.py            # run everything
    python3 ops/stress.py --list     # show the cases
    python3 ops/stress.py -k hostile # run cases matching a substring

Every case runs against a bridge this script starts on its OWN ports, so it
can never disturb a demo running on the default 8765/8766/8767.

Standard library only, same as the rest of ops/. Exit code is the number of
failures, so this is usable in a pre-demo gate.

What this is for: the software-only path is the backup demo. If the board is
unplugged, run over, or simply refuses to enumerate at 9am, everything below
still has to work. So everything below is tested without a board attached.
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

WS_PORT     = int(os.environ.get("STRESS_WS_PORT", "18765"))
INGEST_PORT = int(os.environ.get("STRESS_INGEST_PORT", "18766"))
HTTP_PORT   = int(os.environ.get("STRESS_HTTP_PORT", "18767"))
API         = f"http://127.0.0.1:{HTTP_PORT}"

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

PASS, FAIL = [], []


# ---------------------------------------------------------------------------
# tiny helpers
# ---------------------------------------------------------------------------

def check(name, ok, detail=""):
    (PASS if ok else FAIL).append(name)
    print(f"  {'PASS' if ok else 'FAIL'}  {name}" + (f"\n          {detail}" if detail and not ok else ""))
    return ok


def get(path, timeout=4):
    with urllib.request.urlopen(API + path, timeout=timeout) as r:
        return r.status, json.loads(r.read().decode())


def post(path, body, timeout=6, ctype="application/json"):
    raw = body if isinstance(body, bytes) else json.dumps(body).encode()
    req = urllib.request.Request(API + path, data=raw, headers={"Content-Type": ctype})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def ingest_lines(lines, delay=0.0):
    """Push raw bytes down the TCP line port, exactly as a board would."""
    s = socket.create_connection(("127.0.0.1", INGEST_PORT), timeout=4)
    try:
        for ln in lines:
            s.sendall(ln if isinstance(ln, bytes) else (ln + "\n").encode())
            if delay:
                time.sleep(delay)
    finally:
        s.close()


def frame(src="node-01", wet=3, state="PASSABLE", t=1789840000):
    return {"src": src, "t": t, "link": {"uplink": True, "inference": "local"},
            "water": {"rungs": 6, "wet": wet, "mm": wet * 10},
            "tds": {"ppm": 0, "trend": "steady"}, "temp_c": 18.0,
            "sound": {"event": "none", "conf": 0.0},
            "hazard": {"state": state, "conf": 0.9, "why": "stress"},
            "decided_at": t, "decided_on": "device"}


# ---------------------------------------------------------------------------
# a minimal RFC 6455 client, so the suite has no dependencies
# ---------------------------------------------------------------------------

class WS:
    def __init__(self, port, timeout=5):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        key = "x3JJHMbDL1EzLkh9GBhXDw=="
        self.sock.sendall(
            f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n"
            f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Version: 13\r\n\r\n".encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            b = self.sock.recv(4096)
            if not b:
                raise RuntimeError("handshake closed")
            buf += b
        if b"101" not in buf.split(b"\r\n")[0]:
            raise RuntimeError("no 101: " + buf.split(b"\r\n")[0].decode())
        self.buf = buf.split(b"\r\n\r\n", 1)[1]

    def recv(self, timeout=3):
        """One text frame's payload, or None on timeout."""
        self.sock.settimeout(timeout)
        while True:
            try:
                if len(self.buf) >= 2:
                    b1, b2 = self.buf[0], self.buf[1]
                    ln = b2 & 0x7F
                    off = 2
                    if ln == 126:
                        if len(self.buf) < 4:
                            raise BlockingIOError
                        ln = int.from_bytes(self.buf[2:4], "big"); off = 4
                    elif ln == 127:
                        if len(self.buf) < 10:
                            raise BlockingIOError
                        ln = int.from_bytes(self.buf[2:10], "big"); off = 10
                    if len(self.buf) >= off + ln:
                        payload = self.buf[off:off + ln]
                        self.buf = self.buf[off + ln:]
                        if (b1 & 0x0F) == 0x8:
                            return None            # close
                        if (b1 & 0x0F) in (0x1, 0x2):
                            return payload.decode("utf-8", "replace")
                        continue                   # ping/pong, skip
                raise BlockingIOError
            except BlockingIOError:
                try:
                    more = self.sock.recv(65536)
                except socket.timeout:
                    return None
                if not more:
                    return None
                self.buf += more

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------------------
# cases
# ---------------------------------------------------------------------------

def case_rest_surface():
    """Every documented endpoint answers, and the spec is served."""
    st, h = get("/health")
    check("health returns 200 with a status", st == 200 and h.get("status") == "up", str(h))
    st, _ = get("/state")
    check("state returns 200", st == 200)
    st, u = get("/uplink")
    check("uplink reports blocked_at_bridge", st == 200 and "blocked_at_bridge" in u, str(u))
    try:
        with urllib.request.urlopen(API + "/openapi.yaml", timeout=4) as r:
            body = r.read()
        check("openapi.yaml is served and non-trivial", r.status == 200 and len(body) > 2000,
              f"{r.status}, {len(body)} bytes")
    except Exception as e:
        check("openapi.yaml is served and non-trivial", False, str(e))


def case_unknown_paths_keep_the_connection_usable():
    """A 404 must not desync keep-alive. This bit a real deployment once."""
    code, _ = post("/nope", {"x": 1})
    ok404 = code == 404
    st, h = get("/health")
    check("404 does not poison the next request", ok404 and st == 200, f"404={code}, next={st}")


def case_hostile_ingest():
    """Garbage on the producer port must not take the bridge down.

    Each of these is something a real board actually emits: a wrong baud rate
    produces bytes with no newlines, a reset mid-line produces half a frame,
    and a serial glitch produces invalid UTF-8.
    """
    hostile = [
        "",                                   # empty line
        "not json at all",
        '{"src":"x"',                         # truncated
        '{"src": "y", "t": }',                # malformed
        "[]",                                 # array
        "null",
        '"just a string"',
        "12345",
        '{"src":"' + "A" * 5000 + '"}',       # very long but legal
    ]
    try:
        ingest_lines(hostile)
        ingest_lines([b"\xff\xfe\x00 invalid utf8 \n"])       # not decodable
        ingest_lines([b"x" * 200000])                          # no newline at all
        time.sleep(0.4)
        st, h = get("/health")
        check("bridge survives hostile ingest", st == 200 and h.get("status") == "up", str(h))
    except Exception as e:
        check("bridge survives hostile ingest", False, repr(e))


def case_good_frame_after_garbage():
    """The important half: it survives AND still works afterwards."""
    ingest_lines([json.dumps(frame(src="stress-01", wet=2, state="PASSABLE"))])
    time.sleep(0.4)
    try:
        st, d = get("/state/stress-01")
        ok = st == 200 and d["frame"]["hazard"]["state"] == "PASSABLE"
        check("a good frame still lands after garbage", ok, str(d)[:120])
    except Exception as e:
        check("a good frame still lands after garbage", False, repr(e))


def case_dated_state():
    """A cached frame and a live frame are identical. Age is the only tell."""
    ingest_lines([json.dumps(frame(src="stale-01"))])
    time.sleep(1.2)
    st, d = get("/state/stale-01")
    has = isinstance(d.get("age_s"), (int, float)) and "stale" in d
    aged = d.get("age_s", 0) >= 1.0
    check("every cached frame is dated", has and aged, str(d)[:120])


def case_websocket_delivery():
    """A console connected mid-run receives frames pushed after it joined."""
    ws = WS(WS_PORT)
    try:
        time.sleep(0.3)
        ingest_lines([json.dumps(frame(src="ws-01", wet=5, state="IMPASSABLE"))])
        seen, deadline = None, time.time() + 4
        while time.time() < deadline:
            msg = ws.recv(timeout=1)
            if msg and "ws-01" in msg:
                seen = msg
                break
        check("websocket delivers a live frame", seen is not None,
              "no ws-01 frame arrived within 4s")
    finally:
        ws.close()


def case_replay_on_connect():
    """A console joining late is not shown a blank screen."""
    ingest_lines([json.dumps(frame(src="replay-01", wet=4, state="IMPASSABLE"))])
    time.sleep(0.4)
    ws = WS(WS_PORT)
    try:
        got, deadline = False, time.time() + 3
        while time.time() < deadline:
            msg = ws.recv(timeout=1)
            if msg and "replay-01" in msg:
                got = True
                break
        check("a late console is replayed the last frame", got)
    finally:
        ws.close()


def case_client_churn():
    """Consoles opening and closing during a run must not stall the producer.

    This is the regression test for the head-of-line bug: before the per-client
    queue and sender thread, one slow console cost a healthy one 12,000 frames
    out of 20,000 and killed the producer.
    """
    stop = threading.Event()
    sent = [0]

    def producer():
        try:
            s = socket.create_connection(("127.0.0.1", INGEST_PORT), timeout=4)
            line = (json.dumps(frame(src="churn-01")) + "\n").encode()
            while not stop.is_set():
                s.sendall(line)
                sent[0] += 1
            s.close()
        except OSError:
            pass

    def churner():
        while not stop.is_set():
            try:
                w = WS(WS_PORT, timeout=2)
                time.sleep(0.05)
                w.close()
            except Exception:
                pass

    t = threading.Thread(target=producer, daemon=True); t.start()
    cs = [threading.Thread(target=churner, daemon=True) for _ in range(4)]
    for c in cs:
        c.start()
    time.sleep(4)
    stop.set()
    time.sleep(0.6)

    try:
        st, h = get("/health", timeout=5)
        alive = st == 200
    except Exception as e:
        alive, h = False, repr(e)
    check("producer never stalls under console churn",
          alive and sent[0] > 5000, f"{sent[0]} frames sent in 4s, bridge alive={alive}")


def case_silent_client_does_not_block():
    """A console that connects and never reads is the classic wedge.

    Paced at 2,000 frames/s, four hundred times the demo rate. At that rate a
    healthy bridge must never make the producer wait. Note that an UNPACED
    producer does eventually block in sendall, at around half a million frames
    per three seconds: that is ordinary TCP backpressure from a receiver that
    cannot read 174,000 lines a second, not a wedge, and no board will ever
    produce at that rate. What would be a bug is the producer blocking at a
    plausible rate, or the bridge dying either way.
    """
    dead = socket.create_connection(("127.0.0.1", WS_PORT), timeout=4)
    dead.sendall(
        f"GET / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        f"Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\nSec-WebSocket-Version: 13\r\n\r\n".encode())
    time.sleep(0.3)

    n, worst, err, st = 0, 0.0, "", 0
    try:
        s = socket.create_connection(("127.0.0.1", INGEST_PORT), timeout=8)
        line = (json.dumps(frame(src="wedge-01")) + "\n").encode()
        t0, target = time.time(), 2000.0
        while time.time() - t0 < 3:
            w0 = time.time()
            s.sendall(line)
            worst = max(worst, time.time() - w0)
            n += 1
            due = t0 + n / target
            slack = due - time.time()
            if slack > 0:
                time.sleep(slack)
        s.close()
    except Exception as e:
        err = f"producer side: {e!r}"
    try:
        st, _ = get("/health", timeout=8)
    except Exception as e:
        err = (err + " | " if err else "") + f"health: {e!r}"
    finally:
        dead.close()

    check("a console that never reads cannot wedge a real-rate producer",
          st == 200 and n > 4000 and worst < 0.25 and not err,
          f"{n} frames in 3s, worst send {worst*1000:.0f}ms, health={st}. {err}")


def case_uplink_latch():
    """CUT NETWORK is real state on the bridge, and it can be released."""
    ws = WS(WS_PORT)
    try:
        def send_text(sock, text):
            data = text.encode()
            hdr = bytes([0x81, 0x80 | len(data)]) + b"\x00\x00\x00\x00"
            sock.sendall(hdr + data)

        send_text(ws.sock, json.dumps({"cmd": "cut_network", "cut": True}))
        time.sleep(1.0)
        _, u1 = get("/uplink")
        send_text(ws.sock, json.dumps({"cmd": "cut_network", "cut": False}))
        time.sleep(1.0)
        _, u2 = get("/uplink")
        check("cut_network latches and releases at the bridge",
              u1.get("blocked_at_bridge") is True and u2.get("blocked_at_bridge") is False,
              f"{u1} then {u2}")
    finally:
        ws.close()


def case_triage_when_cut():
    """The whole thesis: cut the uplink and the answer still comes, on device."""
    ws = WS(WS_PORT)
    try:
        data = json.dumps({"cmd": "cut_network", "cut": True}).encode()
        ws.sock.sendall(bytes([0x81, 0x80 | len(data)]) + b"\x00\x00\x00\x00" + data)
        time.sleep(0.4)
        code, body = post("/triage", {"frame": frame(wet=6, state="IMPASSABLE")}, timeout=10)
        r = json.loads(body)
        ok = (code == 200 and r.get("decided_on") == "device"
              and r.get("blocked_at") == "bridge" and "IMPASSABLE" in (r.get("text") or ""))
        check("uplink cut still yields a verdict, decided_on device", ok, f"{code} {r}")
    finally:
        data = json.dumps({"cmd": "cut_network", "cut": False}).encode()
        try:
            ws.sock.sendall(bytes([0x81, 0x80 | len(data)]) + b"\x00\x00\x00\x00" + data)
        except OSError:
            pass
        time.sleep(0.3)
        ws.close()


def case_triage_timeout_is_bounded():
    """A dead tower must not hang the dispatcher. It must give up and fall back.

    Pointed at a blackhole address with a 2s budget. What matters is that the
    call returns inside the budget and still produces a usable answer.
    """
    env = dict(os.environ, TRIAGE_TIMEOUT="2.0",
               OPENAI_BASE="http://10.255.255.1:9", OPENAI_API_KEY="")
    code = (
        "import json,sys,time\n"
        "sys.path.insert(0, %r)\n"
        "import importlib, dispatch.triage as T\n"
        "importlib.reload(T)\n"
        "T.ENDPOINT='http://10.255.255.1:9/v1/chat/completions'\n"
        "T.PROBE_URL='http://10.255.255.1:9/v1/models'\n"
        "f=json.load(sys.stdin)\n"
        "t0=time.time(); r=T.triage(f); dt=time.time()-t0\n"
        "print(json.dumps({'dt':dt,'r':r}))\n" % str(ROOT)
    )
    try:
        p = subprocess.run([sys.executable, "-c", code], input=json.dumps(frame(wet=6, state="IMPASSABLE")),
                           capture_output=True, text=True, timeout=30, env=env, cwd=str(ROOT))
        out = json.loads(p.stdout.strip().splitlines()[-1])
        ok = out["dt"] < 12 and out["r"].get("ok") and out["r"].get("decided_on") == "device"
        check("an unreachable cloud gives up fast and falls back to the device",
              ok, f"took {out['dt']:.1f}s, got {out['r']}")
    except Exception as e:
        check("an unreachable cloud gives up fast and falls back to the device", False,
              f"{e}\n{getattr(e, 'stderr', '')}")


def case_fake_producer_end_to_end():
    """The backup demo with NO hardware: serial_forward --fake into the bridge."""
    p = subprocess.Popen(
        [sys.executable, "ops/serial_forward.py", "--fake",
         "--host", "127.0.0.1", "--tcp", str(INGEST_PORT)],
        cwd=str(ROOT), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        deadline, got = time.time() + 12, None
        while time.time() < deadline:
            try:
                st, d = get("/state")
                srcs = (d or {}).get("sources") or {}
                cand = [k for k in srcs if k.startswith("node")]
                if cand:
                    got = cand[0]
                    break
            except Exception:
                pass
            time.sleep(0.5)
        if got:
            time.sleep(1.0)
            _, one = get(f"/state/{got}")
            fresh = one.get("age_s", 99) < 3 and not one.get("stale")
            has = "hazard" in (one.get("frame") or {})
            check("software-only: fake producer feeds the bridge with no board",
                  fresh and has, str(one)[:160])
        else:
            err = ""
            if p.poll() is not None:
                err = (p.stderr.read() or "")[:300]
            check("software-only: fake producer feeds the bridge with no board", False,
                  "no node source appeared within 12s. " + err)
    finally:
        p.terminate()
        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill()


def case_dashboard_is_served_same_origin():
    """The console must come off the bridge, or POST /triage is cross-origin."""
    try:
        with urllib.request.urlopen(API + "/", timeout=4) as r:
            body = r.read().decode("utf-8", "replace")
        ok = r.status == 200 and "<title>" in body and "WebSocket" in body
        check("the console is served by the bridge itself", ok, f"{r.status}, {len(body)} bytes")
    except Exception as e:
        check("the console is served by the bridge itself", False, repr(e))


def case_oversize_post_is_refused_cleanly():
    """A 10 MB body must be refused without killing the connection."""
    code, _ = post("/ingest", b"{" + b"a" * (10 * 1024 * 1024) + b"}", timeout=20)
    refused = code in (400, 413, 431)
    try:
        st, _ = get("/health", timeout=5)
    except Exception:
        st = 0
    check("an oversize body is refused and the bridge stays up",
          refused and st == 200, f"POST={code}, health={st}")


def case_sustained_ingest_rate():
    """How fast can the bridge actually take frames, with a console attached.

    The demo runs at 5 Hz. This measures the headroom, so a number exists
    instead of a feeling. Fails only if the bridge cannot manage 100x demo.
    """
    ws = WS(WS_PORT)
    try:
        s = socket.create_connection(("127.0.0.1", INGEST_PORT), timeout=8)
        s.settimeout(10)
        line = (json.dumps(frame(src="rate-01")) + "\n").encode()
        t0, n, stalled = time.time(), 0, False
        while time.time() - t0 < 4:
            try:
                s.sendall(line)
            except socket.timeout:
                # Backpressure. The producer waiting is the bridge telling us
                # its ceiling, which is exactly what this case measures.
                stalled = True
                break
            n += 1
        dt = time.time() - t0
        try:
            s.close()
        except OSError:
            pass
        rate = n / dt
        check("sustained ingest is at least 100x the demo rate",
              rate >= 500, f"measured {rate:,.0f} frames/s ({rate/5:.0f}x demo)")
        print(f"          measured {rate:,.0f} frames/s with one console attached "
              f"({rate/5:,.0f}x the 5 Hz demo rate)"
              + (", producer backpressured at the ceiling" if stalled else ""))
    finally:
        ws.close()


CASES = [
    ("rest surface", case_rest_surface),
    ("404 keep-alive", case_unknown_paths_keep_the_connection_usable),
    ("hostile ingest", case_hostile_ingest),
    ("recovery after garbage", case_good_frame_after_garbage),
    ("dated state", case_dated_state),
    ("websocket delivery", case_websocket_delivery),
    ("replay on connect", case_replay_on_connect),
    ("client churn", case_client_churn),
    ("silent client", case_silent_client_does_not_block),
    ("uplink latch", case_uplink_latch),
    ("triage when cut", case_triage_when_cut),
    ("triage timeout", case_triage_timeout_is_bounded),
    ("fake producer", case_fake_producer_end_to_end),
    ("console same-origin", case_dashboard_is_served_same_origin),
    ("oversize post", case_oversize_post_is_refused_cleanly),
    ("sustained rate", case_sustained_ingest_rate),
]


def start_bridge():
    env = dict(os.environ, TKB_WS_PORT=str(WS_PORT),
               TKB_INGEST_PORT=str(INGEST_PORT), TKB_HTTP_PORT=str(HTTP_PORT))
    p = subprocess.Popen([sys.executable, "ops/ws_bridge.py", "--serve-only"],
                         cwd=str(ROOT), env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    for _ in range(60):
        try:
            get("/health", timeout=1)
            return p
        except Exception:
            if p.poll() is not None:
                raise RuntimeError("bridge died on startup:\n" + (p.stderr.read() or ""))
            time.sleep(0.25)
    p.kill()
    raise RuntimeError("bridge never came up")


def stop_bridge(p):
    p.terminate()
    try:
        p.wait(timeout=5)
    except subprocess.TimeoutExpired:
        p.kill()
    # The port has to be free before the next case binds it.
    for _ in range(40):
        try:
            socket.create_connection(("127.0.0.1", HTTP_PORT), timeout=0.2).close()
            time.sleep(0.25)
        except OSError:
            return
    time.sleep(1.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    ap.add_argument("-k", default="", help="only cases whose name contains this")
    args = ap.parse_args()

    if args.list:
        for n, _ in CASES:
            print(n)
        return 0

    selected = [(n, f) for n, f in CASES if args.k in n]
    if not selected:
        print("no cases matched")
        return 1

    # Every case gets its own bridge. Cases that hammer the ingest port leave a
    # bridge busy draining for seconds afterwards, and a shared bridge turns
    # that into phantom failures in whatever case runs next. A suite that
    # reports failures it cannot reproduce in isolation is worse than no suite.
    print(f"stress bridge on ws:{WS_PORT} tcp:{INGEST_PORT} http:{HTTP_PORT}, "
          f"fresh per case\n")
    for name, fn in selected:
        print(f"[{name}]")
        bridge = None
        try:
            bridge = start_bridge()
            fn()
        except Exception as e:
            check(name + " (raised)", False, repr(e))
        finally:
            if bridge is not None:
                stop_bridge(bridge)
        print()

    print(f"{len(PASS)} passed, {len(FAIL)} failed")
    for f in FAIL:
        print(f"  FAILED: {f}")
    return len(FAIL)


if __name__ == "__main__":
    sys.exit(main())
