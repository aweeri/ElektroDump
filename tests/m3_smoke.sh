#!/usr/bin/env bash
# ElektroDump M3 smoke test.
#
# Runs the daemon with the hardware-free `mock` SDR in GGAK mode, which emits
# a synthetic PM/BPSK signal carrying valid 224-byte frames. Verifies that the
# demod locks, SNR is published, CADU is spooled, and `ggak decode` summarises
# the spool. Requires python3 (for HTTP/JSON) but no SDR hardware and no curl.
#
#   ./tests/m3_smoke.sh
set -u

cd "$(dirname "$0")/.."
PORT="${PORT:-18081}"
CFG="$(mktemp)"
LOG="$(mktemp)"
SPOOL="$(mktemp -d)"

cat > "$CFG" <<JSON
{
  "version": 1,
  "sdr": {"driver": "mock", "mock": {"ggak": true}},
  "ggak": {
    "spool_dir": "$SPOOL",
    "mock": {"mod_index": 0.5, "snr_db": 20.0, "carrier_offset_hz": 0.0}
  },
  "web": {"bind": "127.0.0.1", "port": $PORT}
}
JSON

./build/elektrodump run -c "$CFG" > "$LOG" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true; rm -f "$CFG" "$LOG"; rm -rf "$SPOOL"' EXIT

# Give the demod time to acquire and spool a few frames.
sleep 7

get() {
    python3 -c 'import sys,urllib.request; sys.stdout.write(urllib.request.urlopen(sys.argv[1]).read().decode())' "$1"
}

echo "--- GET /api/v1/signal ---"
SIGNAL="$(get "http://127.0.0.1:$PORT/api/v1/signal")"
echo "$SIGNAL"
echo "--- GET /api/v1/status (pipeline) ---"
STATUS="$(get "http://127.0.0.1:$PORT/api/v1/status")"
echo "$STATUS" | python3 -c 'import json,sys; d=json.load(sys.stdin); print(json.dumps(d["pipeline"], indent=2))'

echo "--- CADU spool ---"
find "$SPOOL" -type f -name '*.cadu' -printf '%p %s bytes\n' | head -n 5
CADU_BYTES="$(find "$SPOOL" -type f -name '*.cadu' -printf '%s\n' | awk '{s+=$1} END {print s+0}')"
echo "total CADU bytes: $CADU_BYTES"

echo "--- ggak decode ---"
./build/elektrodump ggak decode "$SPOOL" -c "$CFG"
echo "--- ggak.csv (head) ---"
head -n 5 "$SPOOL/ggak.csv"

# Assertions.
python3 - "$SIGNAL" "$STATUS" "$CADU_BYTES" <<'PY'
import json, sys
signal = json.loads(sys.argv[1])
status = json.loads(sys.argv[2])
cadu_bytes = int(sys.argv[3])
pipeline = status["pipeline"]
ok = True
if signal["snr_db"] <= 0:
    print("FAIL: snr_db <= 0"); ok = False
if not signal["locked"]:
    print("FAIL: not locked"); ok = False
if pipeline["cadus_total"] <= 0:
    print("FAIL: no CADUs"); ok = False
if cadu_bytes <= 0:
    print("FAIL: no CADU bytes on disk"); ok = False
print("M3 smoke:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
PY
