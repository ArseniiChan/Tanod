#!/usr/bin/env python3
"""
Dispatch triage. Two paths, and the whole point of the project is which one
answers when the uplink is gone.

  CLOUD  a real HTTPS call to a real model. It can really fail, and when it
         does the failure is reported verbatim: the status code, the DNS error,
         the timeout. Nothing here simulates an outage.
  DEVICE the verdict the ESP32 already reached on its own, carried in the
         frame's hazard.why. No network, no round trip, always available.

Standard library only, so this runs anywhere the bridge runs.

The timeout is deliberately short. A dispatcher that hangs for thirty seconds
waiting on a dead tower has already failed; the interesting behaviour is how
fast it gives up and what it does next.
"""
import json
import os
import socket
import ssl
import time
import urllib.error
import urllib.request

TIMEOUT_S = float(os.environ.get("TRIAGE_TIMEOUT", "4.0"))
MODEL = os.environ.get("OPENAI_MODEL", "gpt-4o-mini")
ENDPOINT = "https://api.openai.com/v1/chat/completions"
PROBE_URL = "https://api.openai.com/v1/models"

SYSTEM = (
    "You are a flood dispatch triage officer. Given one sensor reading, reply "
    "with ONE sentence under 20 words: whether to send a ground unit through "
    "this crossing, and why. No preamble, no markdown."
)


def _device_answer(frame, reason):
    """What the node already decided, on its own, with no network."""
    hz = (frame or {}).get("hazard") or {}
    why = hz.get("why") or "no verdict in frame"
    state = hz.get("state") or "UNKNOWN"
    return {
        "ok": True,
        "decided_on": "device",
        "source": "device",
        "text": f"{state}: {why}",
        "error": reason,
        "latency_ms": 0,
    }


def _prompt(frame):
    w = frame.get("water") or {}
    t = frame.get("tds") or {}
    hz = frame.get("hazard") or {}
    return (
        f"Depth {w.get('mm')} mm ({w.get('wet')} of {w.get('rungs')} rungs wet). "
        f"Turbidity {t.get('ppm')} ppm, {t.get('trend')}. "
        f"On-device classification: {hz.get('state')}. "
        f"Ground units are impassable above 30 mm."
    )


def triage(frame, blocked=False):
    """Try the cloud for real. Fall back to what the device already knew.

    `blocked` is set when the operator has cut the uplink at the bridge. It is
    reported in the result rather than hidden, so the screen can say where the
    break is instead of implying a failure that did not happen.
    """
    frame = frame or {}
    if blocked:
        r = _device_answer(frame, "uplink cut at the bridge by the operator")
        r["blocked_at"] = "bridge"
        return r

    key = os.environ.get("OPENAI_API_KEY")
    t0 = time.monotonic()

    if not key:
        # No credential, but still make a real call so a real outage still
        # produces a real failure. This proves reachability, not intelligence,
        # and says so.
        try:
            urllib.request.urlopen(PROBE_URL, timeout=TIMEOUT_S)
            reachable = True
        except urllib.error.HTTPError:
            reachable = True          # 401 is a reachable server refusing us
        except Exception as e:
            inner = getattr(e, "reason", e)
            r = _device_answer(frame, f"cloud unreachable: {inner}")
            r["latency_ms"] = int((time.monotonic() - t0) * 1000)
            return r
        if reachable:
            return {
                "ok": True,
                "decided_on": "cloud",
                "source": "probe",
                "text": "cloud reachable, but no OPENAI_API_KEY is set, so no "
                        "triage was requested",
                "error": None,
                "latency_ms": int((time.monotonic() - t0) * 1000),
            }

    body = json.dumps({
        "model": MODEL,
        "messages": [{"role": "system", "content": SYSTEM},
                     {"role": "user", "content": _prompt(frame)}],
        "max_tokens": 60,
        "temperature": 0.2,
    }).encode()
    req = urllib.request.Request(
        ENDPOINT, data=body,
        headers={"Content-Type": "application/json",
                 "Authorization": f"Bearer {key}"})
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT_S) as resp:
            payload = json.loads(resp.read())
        text = payload["choices"][0]["message"]["content"].strip()
        return {
            "ok": True,
            "decided_on": "cloud",
            "source": MODEL,
            "text": text,
            "error": None,
            "latency_ms": int((time.monotonic() - t0) * 1000),
        }
    except urllib.error.HTTPError as e:
        why = f"cloud returned HTTP {e.code}"
    except urllib.error.URLError as e:
        inner = getattr(e, "reason", e)
        if isinstance(inner, socket.timeout):
            why = f"cloud timed out after {TIMEOUT_S:.1f}s"
        else:
            why = f"cloud unreachable: {inner}"
    except socket.timeout:
        why = f"cloud timed out after {TIMEOUT_S:.1f}s"
    except (ssl.SSLError, OSError) as e:
        why = f"cloud unreachable: {type(e).__name__}: {e}"
    except Exception as e:
        why = f"cloud failed: {type(e).__name__}: {e}"

    r = _device_answer(frame, why)
    r["latency_ms"] = int((time.monotonic() - t0) * 1000)
    return r


if __name__ == "__main__":
    import sys
    raw = "" if sys.stdin.isatty() else sys.stdin.read().strip()
    f = json.loads(raw) if raw else {
        "hazard": {"state": "IMPASSABLE",
                   "why": "5 of 6 rungs wet, 50mm exceeds 30mm passable depth"},
        "water": {"rungs": 6, "wet": 5, "mm": 50},
        "tds": {"ppm": 760, "trend": "rising"}}
    print(json.dumps(triage(f), indent=2))
