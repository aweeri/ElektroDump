#!/usr/bin/env bash
# ElektroDump M10 smoke test (deployment: unit file, installer, staged install).
#
# A real Raspberry Pi is not available here, so this validates everything that
# is checkable locally:
#   1. `bash -n deploy/install.sh` (shell syntax).
#   2. systemd unit sanity (required keys, ExecStart matches the real CLI).
#   3. tmpfiles entry creates /var/lib/elektrodump.
#   4. a DESTDIR staged install (no root) lays out the expected tree.
#   5. the staged binary runs (`--help`, `version`).
#   6. re-running the installer keeps an existing config (idempotent upgrade).
#   7. the installer prints a web URL matching the configured port.
#
#   ./tests/m10_smoke.sh
set -u

cd "$(dirname "$0")/.."
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

make >/dev/null 2>&1 || { echo "build failed"; exit 1; }

ok=1

echo "=== 1. shell syntax check (bash -n) ==="
if bash -n deploy/install.sh; then
    echo "install.sh: syntax OK"
else
    echo "FAIL: install.sh syntax"
    ok=0
fi

echo "=== 2. systemd unit sanity ==="
UNIT=deploy/elektrodump.service
for key in 'Type=simple' 'User=elektrodump' 'Group=elektrodump' \
           'WorkingDirectory=/opt/elektrodump' \
           'ExecStart=/opt/elektrodump/elektrodump run --config /etc/elektrodump/config.json' \
           'Restart=always' 'WantedBy=multi-user.target'; do
    if grep -qF "$key" "$UNIT"; then
        echo "unit: $key"
    else
        echo "FAIL: unit missing '$key'"
        ok=0
    fi
done
if grep -qE '^ExecStart=.*[[:space:]]run[[:space:]].*--config[[:space:]]' "$UNIT"; then
    echo "unit: ExecStart uses 'run --config <path>'"
else
    echo "FAIL: ExecStart does not use 'run --config <path>'"
    ok=0
fi

echo "=== 3. tmpfiles sanity ==="
if grep -qE '^d[[:space:]]+/var/lib/elektrodump[[:space:]]' deploy/elektrodump.tmpfiles; then
    echo "tmpfiles: /var/lib/elektrodump entry present"
else
    echo "FAIL: tmpfiles missing /var/lib/elektrodump"
    ok=0
fi

echo "=== 4. staged install (DESTDIR, no root) ==="
if DESTDIR="$STAGE" bash deploy/install.sh >"$STAGE/install.log" 2>&1; then
    cat "$STAGE/install.log"
else
    echo "FAIL: staged install failed"
    cat "$STAGE/install.log"
    ok=0
fi

PREFIX="$STAGE/opt/elektrodump"
for path in "$PREFIX/elektrodump" \
            "$PREFIX/resources/calibration/msugs_table.json" \
            "$PREFIX/resources/luts/ir_gray.png" \
            "$STAGE/etc/systemd/system/elektrodump.service" \
            "$STAGE/etc/tmpfiles.d/elektrodump.conf" \
            "$STAGE/etc/elektrodump/config.json"; do
    if [ -e "$path" ]; then
        echo "staged: ${path#"$STAGE"}"
    else
        echo "FAIL: missing staged path ${path#"$STAGE"}"
        ok=0
    fi
done

echo "=== 5. staged binary runs ==="
if "$PREFIX/elektrodump" --help >/dev/null 2>&1; then
    echo "staged binary: --help OK"
else
    echo "FAIL: staged binary --help"
    ok=0
fi
if "$PREFIX/elektrodump" version 2>/dev/null | grep -q 'elektrodump'; then
    echo "staged binary: version OK"
else
    echo "FAIL: staged binary version"
    ok=0
fi

echo "=== 6. idempotent re-run keeps config ==="
BEFORE="$(cat "$STAGE/etc/elektrodump/config.json")"
if DESTDIR="$STAGE" bash deploy/install.sh >/dev/null 2>&1; then
    AFTER="$(cat "$STAGE/etc/elektrodump/config.json")"
    if [ "$BEFORE" = "$AFTER" ]; then
        echo "re-run: existing config preserved"
    else
        echo "FAIL: re-run clobbered the config"
        ok=0
    fi
else
    echo "FAIL: staged re-run failed"
    ok=0
fi

echo "=== 7. installer web URL matches config port ==="
PORT="$(grep -m1 -oE '"port"[[:space:]]*:[[:space:]]*[0-9]+' \
    "$STAGE/etc/elektrodump/config.json" | grep -oE '[0-9]+$')"
if [ -n "$PORT" ] && grep -q ":$PORT/" "$STAGE/install.log"; then
    echo "installer printed web URL with port $PORT"
else
    echo "FAIL: installer web URL does not match config port ($PORT)"
    ok=0
fi

if [ "$ok" -eq 1 ]; then
    echo "M10 smoke: PASS"
else
    echo "M10 smoke: FAIL"
    exit 1
fi
