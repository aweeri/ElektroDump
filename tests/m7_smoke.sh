#!/usr/bin/env bash
# ElektroDump M7 smoke test (projection, WebP/PPM, product store, products API).
#
#   1. The M6 test binary writes a synthetic full-disk xRIT fixture.
#   2. `elektrodump image` reprojects it to Mercator and stores the products
#      under <out>/products/<YYYY>/<MM>/<DD>/<sat>/<product>/.
#   3. `elektrodump serve` exposes /api/v1/products and /api/v1/products/latest.
#   4. A stale .tmp file is removed on the next start.
#
#   ./tests/m7_smoke.sh
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

echo "=== 2. reproject + store products ==="
python3 - "$OUT" "$LOG" <<'PY'
import glob
import json
import os
import socket
import subprocess
import sys
import time
import urllib.request

out_dir, log_path = sys.argv[1], sys.argv[2]
ok = True

with open(log_path, 'w') as handle:
    rc = subprocess.call(
        ['./build/elektrodump', 'image', 'build/m6_fixture', out_dir],
        stdout=handle, stderr=subprocess.STDOUT)
with open(log_path) as handle:
    sys.stdout.write(handle.read())
if rc != 0:
    print("FAIL: image command exited", rc)
    ok = False

files = glob.glob(os.path.join(out_dir, '**', '*'), recursive=True)
sidecars = [p for p in files
            if p.endswith('.json') and os.sep + 'products' + os.sep in p]
images = [p for p in files if p.endswith(('.webp', '.png', '.ppm'))]
for product in ('visual', 'ir', 'ir_cloud'):
    matches = [p for p in images
               if os.path.basename(p).endswith('_' + product + '.webp') or
               os.path.basename(p).endswith('_' + product + '.png') or
               os.path.basename(p).endswith('_' + product + '.ppm')]
    if not matches:
        print("FAIL: missing product", product)
        ok = False
    else:
        print("product %-9s -> %s" % (product, os.path.relpath(matches[0], out_dir)))

# Check the sidecar georeferencing.
for path in sidecars:
    with open(path) as handle:
        meta = json.load(handle)
    if meta.get('projection') != 'mercator':
        print("FAIL: projection is", meta.get('projection'))
        ok = False
    geo = meta.get('georeferencing')
    if not geo or geo.get('lon_max', 0) <= geo.get('lon_min', 0):
        print("FAIL: missing georeferencing in", path)
        ok = False
    break

# --- 3. products API -------------------------------------------------------
sock = socket.socket()
sock.bind(('127.0.0.1', 0))
port = sock.getsockname()[1]
sock.close()

config_path = os.path.join(out_dir, 'config.json')
with open(config_path, 'w') as handle:
    json.dump({"version": 1, "storage": {"root": out_dir},
               "web": {"bind": "127.0.0.1", "port": port}}, handle)

proc = subprocess.Popen(
    ['./build/elektrodump', 'serve', '--config', config_path],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    base = 'http://127.0.0.1:%d' % port
    data = None
    for _ in range(60):
        try:
            with urllib.request.urlopen(
                    base + '/api/v1/products/latest?product=visual',
                    timeout=1) as response:
                data = response.read()
                break
        except Exception:
            time.sleep(0.1)
    if data is None:
        print("FAIL: /api/v1/products/latest did not respond")
        ok = False
    elif (data[:4] == b'RIFF' or data[:8] == b'\x89PNG\r\n\x1a\n' or
          data[:2] == b'P6'):
        print("latest product: %d bytes, valid image" % len(data))
    else:
        print("FAIL: latest product is not a valid image")
        ok = False

    try:
        with urllib.request.urlopen(base + '/api/v1/products?limit=10',
                                    timeout=2) as response:
            listing = json.loads(response.read().decode('utf-8'))
        if listing.get('total', 0) < 3:
            print("FAIL: /api/v1/products total =", listing.get('total'))
            ok = False
        else:
            print("products list: %d entries" % listing['total'])
    except Exception as exc:
        print("FAIL: /api/v1/products:", exc)
        ok = False
finally:
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

# --- 4. stale .tmp cleanup -------------------------------------------------
stale = os.path.join(out_dir, 'products', 'stale.tmp')
with open(stale, 'w') as handle:
    handle.write('stale')
subprocess.call(['./build/elektrodump', 'image', 'build/m6_fixture', out_dir],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
if os.path.exists(stale):
    print("FAIL: stale .tmp was not cleaned")
    ok = False
else:
    print("stale .tmp cleaned on start")

print("M7 smoke:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
PY
RESULT=$?
rm -rf "$OUT" "$LOG"
exit "$RESULT"
