#!/usr/bin/env python3
"""
Telemetry bridge for Tikbalang. No dependencies, no pip install.

Three ports, three audiences:

  :8765  WebSocket   dashboards subscribe to the live frame stream
  :8766  TCP lines   producers push frames, one JSON object per line
  :8767  HTTP        REST + OpenAPI, for teammates and for curl

Producers reach it three ways:

  1. A local process the bridge starts (the scripted node):

       python3 ops/ws_bridge.py ./build/node --rate 5

     The node is held until a dashboard connects, so the scripted uplink
     failure never happens off-screen. Piping works too, but drops frames
     emitted before the dashboard connects:

       ./build/node --rate 5 | python3 ops/ws_bridge.py

  2. Over TCP :8766, from any machine that can reach this host. This is how a
     board on somebody else's laptop gets onto the same dashboard:

       python3 ops/ws_bridge.py --serve-only
       # elsewhere: python3 ops/serial_forward.py --host <this-ip>

  3. Over HTTP POST /ingest on :8767, for anything that would rather speak
     REST than open a socket:

       curl -X POST http://<host>:8767/ingest -d '{"src":"node-01",...}'

Network producers are never held for a dashboard. Real hardware does not wait
to be watched. The most recent frame per "src" is kept and replayed to each new
dashboard, so the console is never blank on connect and GET /state always
answers.

Open dashboard/index.html and pick LIVE. It connects to ws://localhost:8765 by
default, or ws://<host>:8765 via ?ws= in the URL.

Frames FROM the dashboard (the cut_network command) are printed to stderr, so
the cut button is visible even before the firmware handles it.
"""
import base64, collections, hashlib, json, os, socket, struct, subprocess, sys, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HOST = "0.0.0.0"
WS_PORT, INGEST_PORT, HTTP_PORT = 8765, 8766, 8767
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

# A telemetry frame is a few hundred bytes. Anything past this is a board at the
# wrong baud rate or a sketch that forgot its newline. Buffering that without a
# limit costs one byte of bridge memory per byte of garbage: measured, 100 MB of
# newline-free input took peak RSS from 13 MB to 114 MB before this cap existed.
MAX_LINE = 64 * 1024

# Per-dashboard outbound backlog. A dashboard that stops reading (a backgrounded
# browser tab, a laptop asleep) must not stall producers or other dashboards, so
# each gets its own queue and its own sender thread, and the oldest frames are
# dropped rather than blocking anyone. For live telemetry the newest frame wins.
QUEUE_DEPTH = 512

clients, lock = [], threading.Lock()          # of Client
first_client = threading.Event()
last_by_src, last_lock = {}, threading.Lock()


# --------------------------------------------------------------------------
# framing
# --------------------------------------------------------------------------

def encode(msg: str) -> bytes:
    """Server to client text frame. Never masked, per the spec."""
    payload = msg.encode("utf-8")
    n = len(payload)
    if n < 126:
        header = struct.pack("!BB", 0x81, n)
    elif n < (1 << 16):
        header = struct.pack("!BBH", 0x81, 126, n)
    else:
        header = struct.pack("!BBQ", 0x81, 127, n)
    return header + payload


def decode(conn):
    """Read one client frame. Client frames are always masked."""
    hdr = conn.recv(2)
    if len(hdr) < 2:
        return None
    opcode, ln = hdr[0] & 0x0F, hdr[1] & 0x7F
    if opcode == 0x8:
        return None
    if ln == 126:
        ln = struct.unpack("!H", conn.recv(2))[0]
    elif ln == 127:
        ln = struct.unpack("!Q", conn.recv(8))[0]
    mask = conn.recv(4)
    data = b""
    while len(data) < ln:
        part = conn.recv(ln - len(data))
        if not part:
            return None
        data += part
    return bytes(b ^ mask[i % 4] for i, b in enumerate(data)).decode("utf-8", "replace")


def handshake(conn):
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = conn.recv(4096)
        if not chunk:
            return False
        data += chunk
        if len(data) > MAX_LINE:
            return False
    key = None
    for line in data.split(b"\r\n"):
        if line.lower().startswith(b"sec-websocket-key:"):
            key = line.split(b":", 1)[1].strip()
    if not key:
        return False
    accept = base64.b64encode(hashlib.sha1(key + GUID.encode()).digest())
    conn.send(
        b"HTTP/1.1 101 Switching Protocols\r\n"
        b"Upgrade: websocket\r\nConnection: Upgrade\r\n"
        b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n"
    )
    return True


# --------------------------------------------------------------------------
# dashboards
# --------------------------------------------------------------------------

def listen_on(port, what):
    """Bind, or die loudly. A bridge that limps along with one dead listener
    reports healthy over REST while no dashboard can ever connect, which is a
    worse failure than not starting at all."""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        srv.bind((HOST, port))
    except OSError as e:
        print(f"[bridge] FATAL: cannot bind :{port} for {what} ({e}).\n"
              f"[bridge] Another bridge is probably still running. Find it with:\n"
              f"[bridge]   lsof -nP -iTCP:{port} -sTCP:LISTEN",
              file=sys.stderr)
        os._exit(1)
    srv.listen(8)
    return srv


class Client:
    """One dashboard. Owns a bounded queue and the only thread that writes to
    its socket, so a slow reader can never hold the broadcast lock."""

    def __init__(self, conn, addr):
        self.conn, self.addr = conn, addr
        self.q = collections.deque(maxlen=QUEUE_DEPTH)
        self.wake = threading.Event()
        self.alive = True
        self.dropped = 0
        threading.Thread(target=self._pump, daemon=True).start()

    def put(self, frame: bytes):
        if not self.alive:
            return
        if len(self.q) == self.q.maxlen:
            # The deque evicts the oldest for us. Dropping beats blocking for
            # live telemetry, but it must never be silent: a dashboard that is
            # quietly missing frames looks like a dashboard that is fine.
            self.dropped += 1
            if self.dropped == 1 or self.dropped % 1000 == 0:
                print(f"[bridge] dashboard {self.addr[0]} behind, dropped "
                      f"{self.dropped} frames", file=sys.stderr)
        self.q.append(frame)
        self.wake.set()

    def _pump(self):
        while self.alive:
            if not self.q:
                self.wake.wait(0.5)
                self.wake.clear()
                continue
            try:
                self.conn.sendall(self.q.popleft())
            except Exception:
                self.close()
                return

    def close(self):
        if not self.alive:
            return
        self.alive = False
        self.wake.set()
        with lock:
            if self in clients:
                clients.remove(self)
        try:
            self.conn.close()
        except Exception:
            pass
        tail = f", {self.dropped} frames dropped" if self.dropped else ""
        print(f"[bridge] dashboard {self.addr[0]} disconnected{tail}", file=sys.stderr)


def broadcast(line: str):
    """Hand one telemetry line to every dashboard. Never blocks on a socket."""
    frame = encode(line)
    with lock:
        targets = list(clients)
    for c in targets:
        c.put(frame)
    try:
        obj = json.loads(line)
        src = obj.get("src")
        if src:
            with last_lock:
                last_by_src[src] = line
    except Exception:
        pass                              # malformed frames relay, but do not
                                          # become the replayed state


def serve_client(conn, addr):
    if not handshake(conn):
        conn.close()
        return
    c = Client(conn, addr)
    with lock:
        clients.append(c)
    first_client.set()
    print(f"[bridge] dashboard connected from {addr[0]}", file=sys.stderr)
    with last_lock:
        backlog = list(last_by_src.values())
    for line in backlog:
        c.put(encode(line))
    try:
        while c.alive:
            msg = decode(conn)
            if msg is None:
                break
            print(f"[bridge] from dashboard: {msg}", file=sys.stderr)
    except Exception:
        pass
    finally:
        c.close()


def ws_loop():
    srv = listen_on(WS_PORT, "dashboards")
    print(f"[bridge] dashboards  ws://localhost:{WS_PORT}", file=sys.stderr)
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=serve_client, args=(conn, addr), daemon=True).start()


# --------------------------------------------------------------------------
# producers
# --------------------------------------------------------------------------

def sock_lines(sock, who):
    """Yield newline-delimited text, refusing to buffer an unbounded line."""
    buf = bytearray()
    complaining = False
    while True:
        chunk = sock.recv(65536)
        if not chunk:
            return
        buf += chunk
        while True:
            i = buf.find(b"\n")
            if i < 0:
                break
            line = bytes(buf[:i])
            del buf[:i + 1]
            yield line.decode("utf-8", "replace").strip()
        if len(buf) > MAX_LINE:
            if not complaining:
                print(f"[bridge] {who}: over {MAX_LINE} bytes with no newline. "
                      f"Wrong baud rate? Discarding until one arrives.",
                      file=sys.stderr)
                complaining = True
            buf.clear()
        elif complaining and not buf:
            complaining = False


def ingest_client(conn, addr):
    """One network producer. Plain TCP, one JSON object per line."""
    print(f"[bridge] producer connected from {addr[0]}", file=sys.stderr)
    seen = 0
    try:
        for line in sock_lines(conn, addr[0]):
            if not line:
                continue
            broadcast(line)
            seen += 1
            if seen % 500 == 0:
                print(f"[bridge] {addr[0]}: {seen} frames", file=sys.stderr)
    except Exception as e:
        print(f"[bridge] producer {addr[0]} error: {e}", file=sys.stderr)
    finally:
        try:
            conn.close()
        except Exception:
            pass
        print(f"[bridge] producer {addr[0]} gone after {seen} frames", file=sys.stderr)


def ingest_loop():
    srv = listen_on(INGEST_PORT, "producers")
    print(f"[bridge] producers   tcp://0.0.0.0:{INGEST_PORT}", file=sys.stderr)
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=ingest_client, args=(conn, addr), daemon=True).start()


def relay(stream):
    sent = 0
    for line in stream:
        line = line.strip()
        if not line:
            continue
        broadcast(line)
        sent += 1
        if sent % 500 == 0:
            print(f"[bridge] {sent} frames, {len(clients)} dashboard(s)",
                  file=sys.stderr)


# --------------------------------------------------------------------------
# HTTP, described by ops/openapi.yaml
# --------------------------------------------------------------------------

SPEC = Path(__file__).with_name("openapi.yaml")


class Api(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "tikbalang-bridge"

    def log_message(self, *a):
        pass                                    # stderr belongs to the bridge

    def _send(self, code, body=b"", ctype="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _json(self, code, obj):
        self._send(code, json.dumps(obj).encode() + b"\n")

    def do_OPTIONS(self):
        self._send(204)

    def do_GET(self):
        path = self.path.split("?", 1)[0].rstrip("/") or "/"
        if path == "/health":
            with lock:
                n = len(clients)
            with last_lock:
                srcs = sorted(last_by_src)
            return self._json(200, {"status": "up", "dashboards": n,
                                    "sources": srcs})
        if path == "/state":
            with last_lock:
                items = {k: json.loads(v) for k, v in last_by_src.items()}
            return self._json(200, {"sources": items})
        if path.startswith("/state/"):
            src = path[len("/state/"):]
            with last_lock:
                raw = last_by_src.get(src)
            if raw is None:
                return self._json(404, {"error": "unknown src", "src": src})
            return self._json(200, json.loads(raw))
        if path in ("/openapi.yaml", "/openapi"):
            if not SPEC.exists():
                return self._json(404, {"error": "openapi.yaml not found"})
            return self._send(200, SPEC.read_bytes(), "application/yaml")
        return self._json(404, {"error": "no such endpoint", "path": path})

    def do_POST(self):
        if self.path.split("?", 1)[0].rstrip("/") != "/ingest":
            return self._json(404, {"error": "no such endpoint"})
        n = int(self.headers.get("Content-Length") or 0)
        if n > MAX_LINE:
            return self._json(413, {"error": "body too large", "max": MAX_LINE})
        raw = self.rfile.read(n).decode("utf-8", "replace")
        try:
            obj = json.loads(raw)
        except ValueError as e:
            return self._json(400, {"error": "body is not JSON", "detail": str(e)})
        frames = obj if isinstance(obj, list) else [obj]
        for f in frames:
            if not isinstance(f, dict) or "src" not in f:
                return self._json(422, {"error": 'every frame needs a "src"'})
        for f in frames:
            broadcast(json.dumps(f, separators=(",", ":")))
        return self._json(202, {"accepted": len(frames)})


def http_loop():
    try:
        srv = ThreadingHTTPServer((HOST, HTTP_PORT), Api)
    except OSError as e:
        print(f"[bridge] FATAL: cannot bind :{HTTP_PORT} for REST ({e}).\n"
              f"[bridge]   lsof -nP -iTCP:{HTTP_PORT} -sTCP:LISTEN", file=sys.stderr)
        os._exit(1)
    srv.daemon_threads = True
    print(f"[bridge] rest        http://localhost:{HTTP_PORT}/openapi.yaml",
          file=sys.stderr)
    srv.serve_forever()


# --------------------------------------------------------------------------

def main():
    for fn in (ws_loop, ingest_loop, http_loop):
        threading.Thread(target=fn, daemon=True).start()

    argv = [a for a in sys.argv[1:] if a != "--serve-only"]
    serve_only = "--serve-only" in sys.argv[1:]

    if serve_only or (not argv and sys.stdin.isatty()):
        print("[bridge] serving network producers only", file=sys.stderr)
        threading.Event().wait()
    elif argv:
        print("[bridge] waiting for a dashboard before starting:",
              " ".join(argv), file=sys.stderr)
        first_client.wait()
        proc = subprocess.Popen(argv, stdout=subprocess.PIPE, text=True, bufsize=1)
        try:
            relay(proc.stdout)
        finally:
            proc.terminate()
    else:
        relay(sys.stdin)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
