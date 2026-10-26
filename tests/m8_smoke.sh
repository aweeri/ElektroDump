#!/usr/bin/env bash
# ElektroDump M8 smoke test (schedule CLI editor + schedule REST API + live
# mode switching).
#
#   1. `elektrodump schedule add/list/enable/disable/remove` round-trips
#      through a user config and `config validate` still passes.
#   2. `elektrodump serve` exposes GET/PUT /api/v1/schedule; an invalid PUT is
#      rejected with 400 + field errors, a valid one is applied.
#   3. `elektrodump run` (mock SDR) switches the live pipeline when a schedule
#      entry becomes active, without crashing or deadlocking.
#
#   ./tests/m8_smoke.sh
set -u

cd "$(dirname "$0")/.."
OUT="$(mktemp -d)"
CFG="$OUT/config.json"
LOG="$(mktemp)"

make >/dev/null 2>&1 || { echo "build failed"; exit 1; }

echo "=== 1. schedule CLI round-trip ==="
./build/elektrodump schedule add --name test1 --start 06:00 --end 06:20 \
    --mode lrit --priority 5 -c "$CFG" || { echo "FAIL: schedule add"; exit 1; }
./build/elektrodump schedule list -c "$CFG" || { echo "FAIL: schedule list"; exit 1; }
./build/elektrodump schedule disable -c "$CFG" || { echo "FAIL: schedule disable"; exit 1; }
./build/elektrodump schedule enable -c "$CFG" || { echo "FAIL: schedule enable"; exit 1; }
./build/elektrodump config validate -c "$CFG" || { echo "FAIL: config validate"; exit 1; }
./build/elektrodump schedule remove test1 -c "$CFG" || { echo "FAIL: schedule remove"; exit 1; }
./build/elektrodump config validate -c "$CFG" || { echo "FAIL: config validate after remove"; exit 1; }
echo "CLI round-trip: PASS"

echo "=== 2. schedule REST API ==="
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
    json.dump({"version": 1, "storage": {"root": out_dir},
               "web": {"bind": "127.0.0.1", "port": port}}, handle)

with open(log_path, 'w') as handle:
    proc = subprocess.Popen(
        ['./build/elektrodump', 'serve', '--config', config_path],
        stdout=handle, stderr=subprocess.STDOUT)

base = 'http://127.0.0.1:%d' % port
try:
    ready = False
    for _ in range(60):
        try:
            with urllib.request.urlopen(base + '/api/v1/schedule',
                                        timeout=1) as response:
                data = json.loads(response.read().decode())
                ready = True
                break
        except Exception:
            time.sleep(0.1)
    if not ready:
        print("FAIL: /api/v1/schedule did not respond")
        ok = False
    else:
        sched = data.get('schedule')
        if not sched or 'entries' not in sched:
            print("FAIL: GET /api/v1/schedule missing schedule")
            ok = False
        else:
            print("GET schedule: %d entry(ies)" % len(sched['entries']))

    invalid = {"schedule": {"enabled": True, "timezone": "UTC",
                            "default_mode": "ggak",
                            "entries": [{"name": "bad", "start": "06:00",
                                         "end": "06:00", "mode": "lrit"}]}}
    req = urllib.request.Request(
        base + '/api/v1/schedule', data=json.dumps(invalid).encode(),
        headers={'Content-Type': 'application/json'}, method='PUT')
    try:
        urllib.request.urlopen(req, timeout=2)
        print("FAIL: invalid PUT was accepted")
        ok = False
    except urllib.error.HTTPError as exc:
        body = json.loads(exc.read().decode())
        if (exc.code == 400 and body.get('error') == 'validation_failed' and
                body.get('fields')):
            print("PUT invalid: 400 validation_failed, %d field(s)" %
                  len(body['fields']))
        else:
            print("FAIL: invalid PUT response", exc.code, body)
            ok = False

    valid = {"schedule": {"enabled": True, "timezone": "UTC",
                          "default_mode": "ggak",
                          "entries": [{"name": "ok", "start": "06:00",
                                       "end": "06:20", "mode": "lrit",
                                       "priority": 7}]}}
    req = urllib.request.Request(
        base + '/api/v1/schedule', data=json.dumps(valid).encode(),
        headers={'Content-Type': 'application/json'}, method='PUT')
    try:
        with urllib.request.urlopen(req, timeout=2) as response:
            body = json.loads(response.read().decode())
            status = response.status
        entries = body.get('schedule', {}).get('entries', [])
        if status == 200 and len(entries) == 1 and entries[0]['name'] == 'ok':
            print("PUT valid: 200, applied 1 entry")
        else:
            print("FAIL: valid PUT response", body)
            ok = False
    except Exception as exc:
        print("FAIL: valid PUT:", exc)
        ok = False
finally:
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

print("M8 API:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
PY
API_RC=$?

echo "=== 3. live mode switch (mock SDR) ==="
python3 - "$OUT" "$LOG" <<'PY'
import datetime
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

# A schedule entry that covers "now" so the scheduler switches to LRIT.
now = datetime.datetime.now(datetime.timezone.utc)
start = (now - datetime.timedelta(minutes=10)).strftime('%H:%M')
end = (now + datetime.timedelta(minutes=10)).strftime('%H:%M')

config_path = os.path.join(out_dir, 'run.json')
with open(config_path, 'w') as handle:
    json.dump({
        "version": 1,
        "run": {"mode": "ggak"},
        "sdr": {"driver": "mock", "mock": {"ggak": True}},
        "storage": {"root": out_dir},
        "schedule": {
            "enabled": True, "timezone": "UTC", "default_mode": "ggak",
            "entries": [{"name": "now", "start": start, "end": end,
                         "mode": "lrit", "priority": 10}],
        },
        "web": {"bind": "127.0.0.1", "port": port},
    }, handle)

with open(log_path, 'w') as handle:
    proc = subprocess.Popen(
        ['./build/elektrodump', 'run', '--config', config_path],
        stdout=handle, stderr=subprocess.STDOUT)

base = 'http://127.0.0.1:%d' % port
mode = None
try:
    # Poll the status API for ~6 s; the daemon must stay alive across the swap.
    for _ in range(60):
        try:
            with urllib.request.urlopen(base + '/api/v1/status',
                                        timeout=1) as response:
                status = json.loads(response.read().decode())
            mode = status.get('mode')
            if mode == 'lrit':
                break
        except Exception:
            pass
        if proc.poll() is not None:
            print("FAIL: daemon exited during mode switch (rc=%s)" % proc.returncode)
            ok = False
            break
        time.sleep(0.1)
    if mode == 'lrit':
        print("mode switched to lrit (entry %s-%s UTC)" % (start, end))
    elif ok:
        print("FAIL: mode did not switch to lrit (got %r)" % mode)
        ok = False
finally:
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

print("M8 switch:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
PY
SWITCH_RC=$?

rm -rf "$OUT" "$LOG"
if [ "$API_RC" != "0" ] || [ "$SWITCH_RC" != "0" ]; then
    echo "M8 smoke: FAIL"
    exit 1
fi
echo "M8 smoke: PASS"
