#!/usr/bin/env bash
# ElektroDump M6 smoke test (image assembly, calibration, composites).
#
#   1. The test binary writes a synthetic full-disk xRIT fixture
#      (build/m6_fixture: 4 channels x 4 segments).
#   2. `elektrodump image` assembles, calibrates and writes the visual, ir and
#      ir_cloud products (PNG when libpng is present, else PPM).
#   3. Peak RSS is checked against the 250 MB budget.
#
#   ./tests/m6_smoke.sh
set -u

cd "$(dirname "$0")/.."
OUT="$(mktemp -d)"
LOG="$(mktemp)"

make >/dev/null 2>&1 || { echo "build failed"; exit 1; }
make build/elektrodump_test_test_composite-release >/dev/null 2>&1 || {
    echo "test build failed"; exit 1;
}

echo "=== 1. generate synthetic xRIT fixture ==="
./build/elektrodump_test_test_composite-release >/dev/null 2>&1

echo "=== 2. assemble + calibrate + composite ==="
python3 - "$OUT" "$LOG" <<'PY'
import glob
import os
import resource
import subprocess
import sys

out_dir, log_path = sys.argv[1], sys.argv[2]
with open(log_path, 'w') as handle:
    rc = subprocess.call(
        ['./build/elektrodump', 'image', 'build/m6_fixture', out_dir],
        stdout=handle, stderr=subprocess.STDOUT)
with open(log_path) as handle:
    sys.stdout.write(handle.read())

ok = rc == 0
if not ok:
    print("FAIL: image command exited", rc)

# M7 stores products under <out>/products/<YYYY>/<MM>/<DD>/<sat>/<product>/.
names = [os.path.basename(p)
         for p in glob.glob(os.path.join(out_dir, '**', '*'), recursive=True)]
for product in ('visual', 'ir', 'ir_cloud'):
    matches = [n for n in names
               if n.endswith('_' + product + '.webp') or
               n.endswith('_' + product + '.png') or
               n.endswith('_' + product + '.ppm')]
    if not matches:
        print("FAIL: missing product", product)
        ok = False
    else:
        print("product %-9s -> %s" % (product, matches[0]))

usage = resource.getrusage(resource.RUSAGE_CHILDREN)
peak_mb = usage.ru_maxrss / 1024.0
print("peak RSS: %.1f MB" % peak_mb)
if peak_mb > 250.0:
    print("FAIL: peak RSS exceeds 250 MB")
    ok = False

print("M6 smoke:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
PY
RESULT=$?
rm -rf "$OUT" "$LOG"
exit "$RESULT"
