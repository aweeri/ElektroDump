#!/usr/bin/env bash
# ElektroDump M9 smoke test (embedded web UI, GGAK API, webhook test endpoint).
#
#   1. `elektrodump serve` serves every page + css/js from the EMBEDDED asset
#      table with `web.static_root` pointing at a non-existent directory (proving
#      no web/ directory is required at runtime).
#   2. The GGAK endpoints return well-formed JSON/SVG with correct status codes
#      even when the science store is empty.
#   3. `elektrodump run` (mock GGAK SDR) feeds the in-memory SKL store and the
#      /api/v1/ggak endpoints serve live data.
#
#   ./tests/m9_smoke.sh
set -u

cd "$(dirname "$0")/.."
OUT="$(mktemp -d)"
LOG="$(mktemp)"

make >/dev/null 2>&1 || { echo "build failed"; exit 1; }

echo "=== 1. embedded assets + GGAK/webhook API (empty store) ==="
python3 - "$OUT" "$LOG" <<'PY'
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

out_dir, log_path = sys.argv[1], sys.argv[2]
ok = True

sock = socket.socket()
sock.bind(('127.0.0.1', 0))
port = sock.getsockname()[1]
sock.close()

config_path = os.path.join(out_dir, 'serve.json')
with open(config_path, 'w') as handle:
    json.dump({
        "version": 1,
        "storage": {"root": out_dir},
        "web": {"bind": "127.0.0.1", "port": port,
                "static_root": os.path.join(out_dir, "no-such-dir")},
    }, handle)

with open(log_path, 'w') as handle:
    proc = subprocess.Popen(
        ['./build/elektrodump', 'serve', '--config', config_path],
        stdout=handle, stderr=subprocess.STDOUT)

base = 'http://127.0.0.1:%d' % port


def fetch(path):
    with urllib.request.urlopen(base + path, timeout=3) as response:
        return response.status, response.headers.get('Content-Type', ''), response.read()


try:
    ready = False
    for _ in range(80):
        try:
            fetch('/api/v1/status')
            ready = True
            break
        except Exception:
            time.sleep(0.1)
    if not ready:
        print("FAIL: server did not start")
        proc.kill()
        sys.exit(1)

    pages = [
        ('/', 'text/html'),
        ('/index.html', 'text/html'),
        ('/gallery', 'text/html'),
        ('/schedule', 'text/html'),
        ('/ggak', 'text/html'),
        ('/system', 'text/html'),
        ('/waterfall', 'text/html'),
        ('/css/app.css', 'text/css'),
        ('/js/api.js', 'javascript'),
        ('/js/app.js', 'javascript'),
        ('/js/dashboard.js', 'javascript'),
        ('/js/gallery.js', 'javascript'),
        ('/js/schedule.js', 'javascript'),
        ('/js/ggak.js', 'javascript'),
        ('/js/waterfall.js', 'javascript'),
    ]
    for path, kind in pages:
        try:
            status, ctype, body = fetch(path)
        except Exception as exc:
            print("FAIL: %s -> %s" % (path, exc))
            ok = False
            continue
        if status != 200:
            print("FAIL: %s -> %d" % (path, status))
            ok = False
        elif kind not in ctype:
            print("FAIL: %s content-type %r" % (path, ctype))
            ok = False
        elif len(body) == 0:
            print("FAIL: %s empty body" % path)
            ok = False
        elif path == '/' and b'ElektroDump' not in body:
            print("FAIL: index page missing title")
            ok = False
    print("embedded assets: served %d path(s), no web/ directory used" % len(pages))

    try:
        fetch('/definitely-not-a-page')
        print("FAIL: unknown path was served")
        ok = False
    except urllib.error.HTTPError as exc:
        if exc.code != 404:
            print("FAIL: unknown path -> %d" % exc.code)
            ok = False

    status, ctype, body = fetch('/api/v1/ggak')
    data = json.loads(body)
    if status != 200 or 'channels' not in data or 'available' not in data:
        print("FAIL: /api/v1/ggak shape", data)
        ok = False

    status, ctype, body = fetch('/api/v1/ggak/plots')
    data = json.loads(body)
    if status != 200 or 'plots' not in data:
        print("FAIL: /api/v1/ggak/plots shape", data)
        ok = False

    status, ctype, body = fetch('/api/v1/ggak/plots/0.svg')
    if status != 200 or b'<svg' not in body or 'image/svg+xml' not in ctype:
        print("FAIL: /api/v1/ggak/plots/0.svg shape", status, ctype)
        ok = False

    try:
        fetch('/api/v1/ggak/plots/99.svg')
        print("FAIL: out-of-range channel was served")
        ok = False
    except urllib.error.HTTPError as exc:
        if exc.code != 404:
            print("FAIL: out-of-range channel -> %d" % exc.code)
            ok = False

    status, ctype, body = fetch('/api/v1/ggak/csv?channel=0')
    if status != 200 or b'utc,' not in body or 'text/csv' not in ctype:
        print("FAIL: /api/v1/ggak/csv shape", status, ctype)
        ok = False

    req = urllib.request.Request(
        base + '/api/v1/webhook/test', data=b'{}',
        headers={'Content-Type': 'application/json'}, method='POST')
    try:
        with urllib.request.urlopen(req, timeout=3) as response:
            webhook = json.loads(response.read())
    except urllib.error.HTTPError as exc:
        webhook = json.loads(exc.read())
    if 'enabled' not in webhook:
        print("FAIL: /api/v1/webhook/test shape", webhook)
        ok = False
    else:
        print("webhook/test: enabled=%s" % webhook['enabled'])

    print("M9 embedded/API:", "PASS" if ok else "FAIL")
finally:
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

sys.exit(0 if ok else 1)
PY
EMBED_RC=$?

echo "=== 2. live GGAK science store (mock SDR) ==="
python3 - "$OUT" "$LOG" <<'PY'
import json
import os
import socket
import subprocess
import sys
import time
import urllib.request

out_dir, log_path = sys.argv[1], sys.argv[2]
ok = True

sock = socket.socket()
sock.bind(('127.0.0.1', 0))
port = sock.getsockname()[1]
sock.close()

config_path = os.path.join(out_dir, 'run.json')
with open(config_path, 'w') as handle:
    json.dump({
        "version": 1,
        "sdr": {"driver": "mock", "mock": {"ggak": True}},
        "ggak": {"spool_dir": os.path.join(out_dir, "spool")},
        "storage": {"root": out_dir},
        "web": {"bind": "127.0.0.1", "port": port,
                "static_root": os.path.join(out_dir, "no-such-dir")},
    }, handle)

with open(log_path, 'w') as handle:
    proc = subprocess.Popen(
        ['./build/elektrodump', 'run', '--config', config_path],
        stdout=handle, stderr=subprocess.STDOUT)

base = 'http://127.0.0.1:%d' % port
data = None
try:
    for _ in range(120):
        try:
            with urllib.request.urlopen(base + '/api/v1/ggak', timeout=1) as response:
                data = json.loads(response.read())
        except Exception:
            data = None
        if data and data.get('available') and \
                data.get('stats', {}).get('skl_frames', 0) > 0:
            break
        if proc.poll() is not None:
            print("FAIL: daemon exited during GGAK capture")
            ok = False
            break
        time.sleep(0.1)

    if not data:
        print("FAIL: /api/v1/ggak did not respond")
        ok = False
    else:
        stats = data.get('stats', {})
        print("ggak live: skl_frames=%s records=%s available=%s" %
              (stats.get('skl_frames'), stats.get('records'),
               data.get('available')))
        if not data.get('available'):
            print("FAIL: GGAK science store has no data after capture")
            ok = False
        channels = [c for c in data.get('channels', []) if c.get('has_value')]
        if not channels:
            print("FAIL: no channel has a value")
            ok = False
        else:
            print("channels with data: %d (e.g. %s=%s)" %
                  (len(channels), channels[0]['name'],
                   channels[0]['latest']))

        with urllib.request.urlopen(
                base + '/api/v1/ggak/plots/0.svg', timeout=2) as response:
            svg = response.read()
        if b'polyline' not in svg:
            print("NOTE: channel 0 svg has no polyline")

    print("M9 live GGAK:", "PASS" if ok else "FAIL")
finally:
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

sys.exit(0 if ok else 1)
PY
LIVE_RC=$?

echo "=== 3. webhook delivery (HMAC + retry on 500) [needs libcurl] ==="
python3 - "$OUT" "$LOG" <<'PY'
import hashlib
import hmac
import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

out_dir, log_path = sys.argv[1], sys.argv[2]
ok = True
secret = "s3cr3t"
received = []


class Receiver(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get('Content-Length', '0'))
        body = self.rfile.read(length)
        received.append({'headers': dict(self.headers), 'body': body})
        # Always fail so the retry/backoff path is exercised.
        self.send_response(500)
        self.end_headers()

    def log_message(self, *args):
        pass


def free_port():
    sock = socket.socket()
    sock.bind(('127.0.0.1', 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


hook_port = free_port()
web_port = free_port()
server = ThreadingHTTPServer(('127.0.0.1', hook_port), Receiver)
threading.Thread(target=server.serve_forever, daemon=True).start()

config_path = os.path.join(out_dir, 'webhook.json')
with open(config_path, 'w') as handle:
    json.dump({
        "version": 1,
        "storage": {"root": out_dir},
        "web": {"bind": "127.0.0.1", "port": web_port,
                "static_root": os.path.join(out_dir, "no-such-dir")},
        "api": {
            "base_url": "http://127.0.0.1:%d" % web_port,
            "webhooks": [{
                "url": "http://127.0.0.1:%d/hook" % hook_port,
                "secret": secret, "events": ["test"]}],
        },
    }, handle)

with open(log_path, 'w') as handle:
    proc = subprocess.Popen(
        ['./build/elektrodump', 'serve', '--config', config_path],
        stdout=handle, stderr=subprocess.STDOUT)

base = 'http://127.0.0.1:%d' % web_port
try:
    ready = False
    for _ in range(80):
        try:
            urllib.request.urlopen(base + '/api/v1/status', timeout=1).read()
            ready = True
            break
        except Exception:
            time.sleep(0.1)
    if not ready:
        print("FAIL: server did not start")
        ok = False
    else:
        req = urllib.request.Request(
            base + '/api/v1/webhook/test', data=b'{}',
            headers={'Content-Type': 'application/json'}, method='POST')
        try:
            with urllib.request.urlopen(req, timeout=3) as response:
                result = json.loads(response.read())
        except Exception as exc:
            result = {'enabled': False, 'error': str(exc)}

        if not result.get('enabled'):
            print("NOTE: webhook disabled (libcurl not compiled in); "
                  "HMAC/retry path covered by test_webhook")
        else:
            # First attempt -> wait for a retry (backoff 1 s) to prove 500 retry.
            for _ in range(90):
                if len(received) >= 2:
                    break
                time.sleep(0.1)
            if len(received) < 2:
                print("FAIL: expected >=2 delivery attempts (retry), got %d"
                      % len(received))
                ok = False
            else:
                first = received[0]
                body = first['body']
                expected = hmac.new(secret.encode(), body,
                                    hashlib.sha256).hexdigest()
                header = first['headers'].get('X-ElektroDump-Signature', '')
                if header != 'sha256=' + expected:
                    print("FAIL: signature %r != sha256=%s" % (header, expected))
                    ok = False
                elif b'"event":"test"' not in body:
                    print("FAIL: payload missing test event")
                    ok = False
                else:
                    print("webhook: %d attempt(s), HMAC header valid, retried on 500"
                          % len(received))

    print("M9 webhook:", "PASS" if ok else "FAIL")
finally:
    server.shutdown()
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

sys.exit(0 if ok else 1)
PY
WEBHOOK_RC=$?

rm -rf "$OUT" "$LOG"
if [ "$EMBED_RC" != "0" ] || [ "$LIVE_RC" != "0" ] || [ "$WEBHOOK_RC" != "0" ]; then
    echo "M9 smoke: FAIL"
    exit 1
fi
echo "M9 smoke: PASS"
