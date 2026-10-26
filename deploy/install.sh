#!/usr/bin/env bash
# ElektroDump installer (M10).
#
# Installs the binary, resources, systemd unit and tmpfiles entry, seeds the
# config and (re)starts the service. Idempotent: safe to re-run for upgrades.
#
# Usage:
#   sudo deploy/install.sh                 # install/upgrade on the target host
#   DESTDIR=/tmp/stage deploy/install.sh   # staged install (no root, no systemd)
#
# Environment overrides:
#   DESTDIR            staging prefix (empty = real system install)
#   ED_SRC_ROOT        source tree root (default: parent of this script)
#   ED_BINARY          built binary (default: $ED_SRC_ROOT/build/elektrodump)
#   ED_RESOURCES       resources/ (default: $ED_SRC_ROOT/resources)
#   ED_CONFIG_EXAMPLE  seed config (default: $ED_SRC_ROOT/config/config.example.json)
#   ED_PREFIX          install prefix (default: /opt/elektrodump)
#   ED_SYSCONF         config dir (default: /etc/elektrodump)
#   ED_SERVICE_USER    service account (default: elektrodump)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_ROOT="${ED_SRC_ROOT:-$(dirname "$SCRIPT_DIR")}"

PREFIX="${ED_PREFIX:-/opt/elektrodump}"
SYSCONF="${ED_SYSCONF:-/etc/elektrodump}"
SERVICE_USER="${ED_SERVICE_USER:-elektrodump}"
SERVICE_GROUP="$SERVICE_USER"
UNIT_NAME="elektrodump.service"
TMPFILES_NAME="elektrodump.conf"

BIN_SRC="${ED_BINARY:-$SRC_ROOT/build/elektrodump}"
RES_SRC="${ED_RESOURCES:-$SRC_ROOT/resources}"
CONFIG_EXAMPLE="${ED_CONFIG_EXAMPLE:-$SRC_ROOT/config/config.example.json}"
UNIT_SRC="$SCRIPT_DIR/elektrodump.service"
TMPFILES_SRC="$SCRIPT_DIR/elektrodump.tmpfiles"

DESTDIR="${DESTDIR:-}"
STAGED=0
if [ -n "$DESTDIR" ]; then
    STAGED=1
fi

# All final paths are prefixed with DESTDIR (empty for a real install).
rooted() { printf '%s%s' "$DESTDIR" "$1"; }

log() { printf '[elektrodump] %s\n' "$*"; }
die() { printf '[elektrodump] error: %s\n' "$*" >&2; exit 1; }

# read_cfg <key> <file>: first string/number value for "key" in a JSON file.
read_cfg() {
    awk -v key="\"$1\"" '
        {
            pos = index($0, key)
            if (pos > 0) {
                rest = substr($0, pos + length(key))
                if (match(rest, /:[ \t]*("[^"]*"|[0-9]+)/)) {
                    v = substr(rest, RSTART, RLENGTH)
                    sub(/^:[ \t]*/, "", v)
                    gsub(/"/, "", v)
                    print v
                    exit
                }
            }
        }' "$2"
}

# --- preconditions ---------------------------------------------------------
if [ "$STAGED" -eq 0 ] && [ "$(id -u)" -ne 0 ]; then
    die "must be run as root (try: sudo $0)"
fi

[ -f "$BIN_SRC" ] || die "binary not found: $BIN_SRC (run 'make' first)"
[ -d "$RES_SRC" ] || die "resources not found: $RES_SRC"
[ -f "$UNIT_SRC" ] || die "unit file not found: $UNIT_SRC"
[ -f "$TMPFILES_SRC" ] || die "tmpfiles file not found: $TMPFILES_SRC"

# --- service account -------------------------------------------------------
if [ "$STAGED" -eq 0 ]; then
    if ! getent group "$SERVICE_GROUP" >/dev/null 2>&1; then
        groupadd --system "$SERVICE_GROUP"
        log "created group $SERVICE_GROUP"
    fi
    if ! id -u "$SERVICE_USER" >/dev/null 2>&1; then
        useradd --system --gid "$SERVICE_GROUP" --home-dir "$PREFIX" \
            --no-create-home --shell /usr/sbin/nologin "$SERVICE_USER"
        log "created user $SERVICE_USER"
    fi
fi

# --- binary + resources ----------------------------------------------------
install -d -m 0755 "$(rooted "$PREFIX")"
install -m 0755 "$BIN_SRC" "$(rooted "$PREFIX")/elektrodump"
log "installed binary -> $PREFIX/elektrodump"

install -d -m 0755 "$(rooted "$PREFIX")/resources"
cp -R "$RES_SRC/." "$(rooted "$PREFIX")/resources/"
log "installed resources -> $PREFIX/resources"

# Runtime data: the relative spool/log/output paths in the config resolve here
# because the unit sets WorkingDirectory=$PREFIX.
install -d -m 0755 "$(rooted "$PREFIX")/data"

# --- config (never clobber an existing file) -------------------------------
install -d -m 0755 "$(rooted "$SYSCONF")"
if [ -f "$(rooted "$SYSCONF")/config.json" ]; then
    log "keeping existing config -> $SYSCONF/config.json"
elif [ -f "$CONFIG_EXAMPLE" ]; then
    install -m 0644 "$CONFIG_EXAMPLE" "$(rooted "$SYSCONF")/config.json"
    log "seeded config -> $SYSCONF/config.json"
else
    die "no config at $SYSCONF/config.json and no seed at $CONFIG_EXAMPLE"
fi

# --- systemd unit + tmpfiles ----------------------------------------------
install -d -m 0755 "$(rooted /etc/systemd/system)"
install -m 0644 "$UNIT_SRC" "$(rooted /etc/systemd/system)/$UNIT_NAME"
log "installed unit -> /etc/systemd/system/$UNIT_NAME"

install -d -m 0755 "$(rooted /etc/tmpfiles.d)"
install -m 0644 "$TMPFILES_SRC" "$(rooted /etc/tmpfiles.d)/$TMPFILES_NAME"
log "installed tmpfiles -> /etc/tmpfiles.d/$TMPFILES_NAME"

# --- ownership + activation ------------------------------------------------
if [ "$STAGED" -eq 0 ]; then
    chown -R "$SERVICE_USER:$SERVICE_GROUP" "$PREFIX"
    chown "$SERVICE_USER:$SERVICE_GROUP" "$SYSCONF/config.json"
    chmod 0640 "$SYSCONF/config.json"

    systemd-tmpfiles --create "/etc/tmpfiles.d/$TMPFILES_NAME"
    systemctl daemon-reload
    systemctl enable --now "$UNIT_NAME"
    log "service enabled and started"
fi

# --- summary ---------------------------------------------------------------
CONFIG_FILE="$(rooted "$SYSCONF")/config.json"
PORT="$(read_cfg port "$CONFIG_FILE" || true)"
[ -n "$PORT" ] || PORT=8080
LOG_FILE="$(read_cfg file "$CONFIG_FILE" || true)"
[ -n "$LOG_FILE" ] || LOG_FILE="data/logs/elektrodump.log"
case "$LOG_FILE" in
    /*) LOG_PATH="$LOG_FILE" ;;
    *) LOG_PATH="$PREFIX/$LOG_FILE" ;;
esac

log "web UI:  http://<host>:$PORT/"
log "logs:    $LOG_PATH"
log "config:  $SYSCONF/config.json"
if [ "$STAGED" -eq 1 ]; then
    log "staged install under DESTDIR=$DESTDIR (systemd not touched)"
fi
