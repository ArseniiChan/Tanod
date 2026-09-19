#!/usr/bin/env python3
"""
Bridge stdin lines to a WebSocket. No dependencies, no pip install.

    python3 ops/ws_bridge.py ./build/node --rate 5

The node is held until the dashboard connects, so the scripted uplink failure
never happens off-screen. Piping also works, but then frames emitted before the
dashboard connects are dropped:

    ./build/node --rate 5 | python3 ops/ws_bridge.py

Then open dashboard/index.html and pick LIVE. It connects to ws://localhost:8765.

Also accepts frames FROM the dashboard (the cut_network command) and prints them
to stderr, so the cut button is visible even before the firmware handles it.
"""
import base64, hashlib, socket, struct, subprocess, sys, threading

HOST, PORT = "0.0.0.0", 8765
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

clients, lock = [], threading.Lock()
first_client = threading.Event()


def handshake(conn):
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = conn.recv(4096)
        if not chunk:
            return False
        data += chunk
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


def serve_client(conn, addr):
    if not handshake(conn):
        conn.close()
        return
    with lock:
        clients.append(conn)
    first_client.set()
    print(f"[bridge] dashboard connected from {addr[0]}", file=sys.stderr)
    try:
        while True:
            msg = decode(conn)
            if msg is None:
                break
            print(f"[bridge] from dashboard: {msg}", file=sys.stderr)
    except Exception:
        pass
    finally:
        with lock:
            if conn in clients:
                clients.remove(conn)
        conn.close()
        print("[bridge] dashboard disconnected", file=sys.stderr)


def accept_loop():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, PORT))
    srv.listen(8)
    print(f"[bridge] listening on ws://localhost:{PORT}", file=sys.stderr)
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=serve_client, args=(conn, addr), daemon=True).start()


def relay(stream):
    sent = 0
    for line in stream:
        line = line.strip()
        if not line:
            continue
        frame = encode(line)
        with lock:
            for c in list(clients):
                try:
                    c.send(frame)
                except Exception:
                    clients.remove(c)
        sent += 1
        if sent % 50 == 0:
            print(f"[bridge] {sent} frames, {len(clients)} client(s)", file=sys.stderr)


def main():
    threading.Thread(target=accept_loop, daemon=True).start()
    cmd = sys.argv[1:]
    if cmd:
        # Hold the node until the dashboard is actually watching, so the
        # scripted uplink failure never happens off-screen.
        print("[bridge] waiting for the dashboard before starting:",
              " ".join(cmd), file=sys.stderr)
        first_client.wait()
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True,
                                bufsize=1)
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
