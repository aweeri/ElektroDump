#!/usr/bin/env bash
# ElektroDump M2 smoke test.
#
# Runs the daemon with the hardware-free `mock` SDR backend and exercises the
# status API. Requires python3 (for HTTP) but no SDR hardware and no curl.
#
#   ./tests/m2_smoke.sh
set -u

cd "$(dirname "$0")/.."
PORT="${PORT:-18080}"
CFG="$(mktemp)"
LOG="$(mktemp)"

cat > "$CFG" <<JSON
{"version":1,"sdr":{"driver":"mock"},"web":{"bind":"127.0.0.1","port":$PORT}}
JSON

./build/elektrodump run -c "$CFG" > "$LOG" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true; rm -f "$CFG"' EXIT

sleep 2

get() {
    python3 -c 'import sys,urllib.request; sys.stdout.write(urllib.request.urlopen(sys.argv[1]).read().decode())' "$1"
}

echo "--- GET /api/v1/status ---"
get "http://127.0.0.1:$PORT/api/v1/status"; echo
echo "--- GET /api/v1/signal ---"
get "http://127.0.0.1:$PORT/api/v1/signal"; echo
echo "--- GET /api/v1/fft (truncated) ---"
get "http://127.0.0.1:$PORT/api/v1/fft" | head -c 200; echo
echo "--- GET /api/v1/health ---"
get "http://127.0.0.1:$PORT/api/v1/health"; echo
echo "--- GET /api/v1/waterfall?rows=2 (truncated) ---"
get "http://127.0.0.1:$PORT/api/v1/waterfall?rows=2" | head -c 200; echo
echo "--- GET /api/v1/config (truncated) ---"
get "http://127.0.0.1:$PORT/api/v1/config" | head -c 200; echo
echo "--- GET / (truncated) ---"
get "http://127.0.0.1:$PORT/" | head -c 80; echo
echo "--- GET /api/v1/nope (status code) ---"
python3 -c 'import sys,urllib.request,urllib.error
try:
    urllib.request.urlopen(sys.argv[1])
except urllib.error.HTTPError as e:
    print(e.code)' "http://127.0.0.1:$PORT/api/v1/nope"

echo "--- daemon log ---"
cat "$LOG"
rm -f "$LOG"
