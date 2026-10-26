#!/usr/bin/env bash
# ElektroDump M5 smoke test (HRIT OQPSK decode chain), fully hardware-free.
#
#   1. `elektrodump decode --mode hrit` over a synthetic OQPSK IQ fixture (the
#      test binary writes build/m5_fixture) and checks the assembled xRIT file.
#   2. The daemon in mock-HRIT mode: a synthetic RRC-shaped OQPSK signal (with a
#      carrier offset, exercising the coarse acquisition) is demodulated,
#      FEC-decoded, demultiplexed and written as xRIT files. The status API is
#      queried to confirm the HRIT pipeline is live.
#
#   ./tests/m5_smoke.sh
set -u

cd "$(dirname "$0")/.."
PORT="${PORT:-18083}"
OUT_DEC="$(mktemp -d)"
OUT_RUN="$(mktemp -d)"
LOG="$(mktemp)"
CFG_DEC="$(mktemp)"
CFG_RUN="$(mktemp)"

make >/dev/null 2>&1 || { echo "build failed"; exit 1; }
make build/elektrodump_test_test_m5-release >/dev/null 2>&1 || { echo "test build failed"; exit 1; }

echo "=== 1. offline HRIT decode over synthetic OQPSK IQ fixture ==="
./build/elektrodump_test_test_m5-release >/dev/null 2>&1   # writes build/m5_fixture
cat > "$CFG_DEC" <<JSON
{ "version": 1, "hrit": {"output_dir": "$OUT_DEC"} }
JSON
./build/elektrodump decode build/m5_fixture --mode hrit -c "$CFG_DEC"

python3 - "$OUT_DEC" build/m5_fixture/expected.lrit <<'PY'
import sys, os, glob
out_dir, expected = sys.argv[1], sys.argv[2]
exp = open(expected, 'rb').read()
got = sorted(glob.glob(os.path.join(out_dir, '*.lrit')))
ok = False
for g in got:
    data = open(g, 'rb').read()
    if data[:len(exp)] == exp:
        ok = True
if not got:
    print("FAIL: decode produced no xRIT files"); sys.exit(1)
print("decoded %d file(s); assembled matches expected: %s" % (len(got), ok))
sys.exit(0 if ok else 1)
PY
DEC_RC=$?
if [ "$DEC_RC" != "0" ]; then echo "offline decode check: FAIL"; fi

echo "=== 2. daemon in mock-HRIT mode ==="
cat > "$CFG_RUN" <<JSON
{
  "version": 1,
  "run": {"mode": "hrit"},
  "sdr": {"driver": "mock", "mock": {"hrit": true}},
  "hrit": {
    "output_dir": "$OUT_RUN",
    "mock": {"snr_db": 20.0, "carrier_offset_hz": 25000.0}
  },
  "web": {"bind": "127.0.0.1", "port": $PORT}
}
JSON
./build/elektrodump run -c "$CFG_RUN" > "$LOG" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true; rm -f "$CFG_RUN" "$CFG_DEC" "$LOG"; rm -rf "$OUT_DEC" "$OUT_RUN"' EXIT
sleep 16

get() {
    python3 -c 'import sys,urllib.request; sys.stdout.write(urllib.request.urlopen(sys.argv[1]).read().decode())' "$1"
}
STATUS="$(get "http://127.0.0.1:$PORT/api/v1/status")"
echo "$STATUS" | python3 -c 'import json,sys; d=json.load(sys.stdin); print("mode:", d.get("mode")); print("pipeline:", json.dumps(d.get("pipeline"), indent=2))'

python3 - "$OUT_RUN" "$STATUS" "$DEC_RC" <<'PY'
import sys, os, glob, json
out_dir, status, dec_rc = sys.argv[1], json.loads(sys.argv[2]), int(sys.argv[3])
lrit = sorted(glob.glob(os.path.join(out_dir, '*.lrit')))
pipeline = status.get("pipeline", {})
ok = True
if dec_rc != 0:
    print("FAIL: offline decode check failed"); ok = False
if len(lrit) == 0:
    print("FAIL: daemon produced no xRIT files"); ok = False
else:
    total = sum(os.path.getsize(f) for f in lrit)
    print("daemon wrote %d xRIT file(s), %d bytes" % (len(lrit), total))
if pipeline.get("files_total", 0) <= 0:
    print("FAIL: pipeline files_total <= 0"); ok = False
if not pipeline.get("locked"):
    print("FAIL: pipeline not locked"); ok = False
print("M5 smoke:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
PY
