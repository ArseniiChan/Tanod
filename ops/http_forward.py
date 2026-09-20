#!/usr/bin/env python3
"""
Forward serial telemetry to the bridge over HTTP POST /ingest instead of the
raw TCP line protocol on :8766.

This exists for one case: the machine holding the board is not on the same
network as the bridge, so it cannot open a TCP socket to it. An HTTP tunnel
forwards HTTP and nothing else, so :8766 is unreachable through one and
POST /ingest on :8767 is not.

    python3 ops/http_forward.py --url https://xxxx.loca.lt
    python3 ops/http_forward.py --url https://xxxx.loca.lt --fake
    python3 ops/http_forward.py --url http://10.189.66.227:8767 --port /dev/cu.usbmodemXXXX

On the same LAN, use serial_forward.py instead. The OpenAPI document says why:
POST costs a request/response round trip per frame, and the TCP line protocol
does not. This batches frames to claw some of that back, at the cost of up to
--interval seconds of extra latency.

Serial reading, port autodetection and the fake generator are imported from
serial_forward rather than copied, for the same reason the sketch symlinks
classify.cpp: two copies of the same logic drift.
"""
import argparse, json, sys, time, urllib.error, urllib.request

sys.path.insert(0, __import__("os").path.dirname(__file__))
import serial_forward as sf


def post(url, frames, timeout, echo):
    """One POST. /ingest takes a single frame or an array, so a batch is one
    round trip. Returns how many the bridge said it accepted, or -1 on a
    transport failure, which the caller treats as 'drop it and keep reading'
    rather than a reason to stop."""
    body = json.dumps(frames if len(frames) > 1 else frames[0]).encode()
    req = urllib.request.Request(
        url.rstrip("/") + "/ingest", data=body, method="POST",
        headers={"Content-Type": "application/json",
                 # localtunnel shows a browser interstitial without this. It is
                 # ignored by every other host, so it is unconditional.
                 "bypass-tunnel-reminder": "1",
                 "User-Agent": "tanod-http-forward"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            n = json.loads(r.read() or b"{}").get("accepted", len(frames))
            if echo:
                print(f"[http] {r.status} accepted {n}", file=sys.stderr)
            return n
    except urllib.error.HTTPError as e:
        print(f"[http] {e.code} {e.reason}: {e.read()[:200]!r}", file=sys.stderr)
    except Exception as e:
        print(f"[http] {type(e).__name__}: {e}", file=sys.stderr)
    return -1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", required=True,
                    help="bridge base URL, e.g. https://xxxx.loca.lt")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--fake", action="store_true")
    ap.add_argument("--echo", action="store_true")
    ap.add_argument("--batch", type=int, default=5,
                    help="frames per POST (default 5, so 5 Hz is 1 req/s)")
    ap.add_argument("--interval", type=float, default=1.0,
                    help="max seconds to hold a partial batch (default 1.0)")
    ap.add_argument("--timeout", type=float, default=8.0)
    a = ap.parse_args()

    if a.list:
        found = sf.candidate_ports()
        print("\n".join(found) if found else "no serial ports found")
        return

    if a.fake:
        print("[http] --fake, no serial device opened", file=sys.stderr)

    def source_lines():
        if a.fake:
            yield from sf.fake_lines()
            return
        while True:
            try:
                port = sf.open_serial(sf.pick_port(a.port), a.baud)
            except SystemExit:
                raise
            except OSError as e:
                print(f"[http] cannot open port ({e}), retry in 2s", file=sys.stderr)
                time.sleep(2)
                continue
            try:
                yield from sf.read_lines(port)
            except Exception as e:
                print(f"[http] serial dropped ({type(e).__name__}: {e}), "
                      f"reopening in 2s", file=sys.stderr)
            finally:
                try:
                    port.close()
                except Exception:
                    pass
            time.sleep(2)

    print(f"[http] posting to {a.url.rstrip('/')}/ingest", file=sys.stderr)
    batch, last_flush = [], time.time()
    sent = bad = failed = 0

    for line in source_lines():
        if not line:
            continue
        line = line.strip()
        if not line:
            continue
        try:
            frame = json.loads(line)
        except (ValueError, TypeError):
            # Same contract as serial_forward: a stray Serial.println is a
            # complaint, not a corrupted feed.
            bad += 1
            if bad <= 5 or bad % 50 == 0:
                print(f"[http] not JSON, dropped ({bad}): {line[:90]!r}",
                      file=sys.stderr)
            continue

        batch.append(frame)
        now = time.time()
        if len(batch) >= a.batch or (now - last_flush) >= a.interval:
            n = post(a.url, batch, a.timeout, a.echo)
            if n < 0:
                failed += 1
            else:
                sent += n
            batch, last_flush = [], now
            if a.echo and sent and sent % 50 == 0:
                print(f"[http] sent {sent}, bad {bad}, failed posts {failed}",
                      file=sys.stderr)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
