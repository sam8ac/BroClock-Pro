# CoinGecko HTTP relay for BroClock Pro.
# The ESP8266 cannot hold a BearSSL session reliably (needs ~13.5k of a ~14-16k
# heap), and every crypto API now forces HTTPS.  This tiny relay runs on the
# always-on PC, talks HTTPS to CoinGecko and serves the SAME JSON to the clock
# over plain HTTP - so the device needs no TLS at all for prices/candles.
#
# Usage: pythonw cgrelay.py   (listens on 0.0.0.0:8099)
# Device: /api/set?key=relay&value=192.168.0.104:8099
import io, json, os, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse
import requests

PORT = 8099
UPSTREAM = "https://api.coingecko.com"
CACHE_TTL = 20.0          # seconds: protects the free tier from bursts
UA = {"User-Agent": "BroClockRelay/1.0", "Accept": "application/json"}
LOG = r"C:\Temp\opencode\cgrelay.log"

_lock = threading.Lock()
_cache = {}               # path+query -> (timestamp, status, body, ctype)


def log(msg):
    try:
        if os.path.exists(LOG) and os.path.getsize(LOG) > 512 * 1024:
            os.replace(LOG, LOG + ".1")
        with io.open(LOG, "a", encoding="utf-8") as f:
            f.write("%s %s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), msg))
    except Exception:
        pass


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass                                   # quiet: our own log

    def do_GET(self):
        p = urlparse(self.path)
        if p.path in ("/", "/ping"):
            return self._send(200, b'{"relay":"ok"}', "application/json")
        if not p.path.startswith("/api/v3/"):
            return self._send(404, b'{"error":"not found"}', "application/json")

        key = p.path + ("?" + p.query if p.query else "")
        now = time.time()
        with _lock:
            hit = _cache.get(key)
        if hit and now - hit[0] < CACHE_TTL:
            return self._send(hit[1], hit[2], hit[3], cached=True)

        url = UPSTREAM + key
        try:
            r = requests.get(url, headers=UA, timeout=10)
            body = r.content
            ctype = r.headers.get("Content-Type", "application/json")
            status = r.status_code
            with _lock:
                _cache[key] = (now, status, body, ctype)
                if len(_cache) > 64:
                    oldest = min(_cache, key=lambda k: _cache[k][0])
                    _cache.pop(oldest, None)
            log("GET %s -> %d (%d B)" % (key, status, len(body)))
            return self._send(status, body, ctype)
        except Exception as exc:
            # stale-on-error: a CoinGecko hiccup must not blank the clock
            with _lock:
                hit = _cache.get(key)
            if hit:
                log("GET %s -> upstream error (%s), serving stale" % (key, exc.__class__.__name__))
                return self._send(hit[1], hit[2], hit[3], cached=True)
            log("GET %s -> ERROR %s" % (key, exc))
            return self._send(502, b'{"error":"upstream"}', "application/json")

    def _send(self, code, body, ctype, cached=False):
        try:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            if cached:
                self.send_header("X-Relay-Cache", "hit")
            self.end_headers()
            self.wfile.write(body)
        except Exception:
            pass


if __name__ == "__main__":
    log("relay start on 0.0.0.0:%d" % PORT)
    srv = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    srv.daemon_threads = True
    srv.serve_forever()
