# ElektroDump deployment (M10)

Everything needed to run ElektroDump as a systemd service on a Raspberry Pi
(or any systemd host).

| File | Purpose |
|---|---|
| [`elektrodump.service`](elektrodump.service:1) | systemd unit (`Type=simple`, dedicated user, restart-on-failure, resource limits) |
| [`install.sh`](install.sh:1) | idempotent installer (binary, resources, config, unit, tmpfiles) |
| [`elektrodump.tmpfiles`](elektrodump.tmpfiles:1) | creates `/var/lib/elektrodump` with the right owner/mode |

## Installed layout

| Path | Contents |
|---|---|
| `/opt/elektrodump/elektrodump` | the self-contained binary (web assets embedded) |
| `/opt/elektrodump/resources/` | calibration table, LUTs, maps |
| `/opt/elektrodump/data/` | relative spool/log/output paths from the config |
| `/etc/elektrodump/config.json` | seeded from `config/config.example.json` (never overwritten) |
| `/etc/systemd/system/elektrodump.service` | the unit |
| `/etc/tmpfiles.d/elektrodump.conf` | runtime directory definition |
| `/var/lib/elektrodump/` | `storage.root`: products, spool, state |

The unit sets `WorkingDirectory=/opt/elektrodump`, so the relative resource
paths in the config (`resources/calibration/...`, `resources/luts/...`,
`resources/maps/...`) resolve without editing the config.

## Install

From the build host (cross-compile first for a 64-bit Pi):

```sh
make CROSS=aarch64-linux-gnu- PI=1
make deploy-pi PI_HOST=pi@elektro.local
```

`deploy-pi` copies the binary, `resources/`, `deploy/` and
`config/config.example.json` to `/tmp/elektrodump-deploy` on the Pi and runs
`deploy/install.sh` there with `sudo`. `make deploy-pi-cross` does the
cross-build and the deploy in one step.

On the Pi itself, from a checkout:

```sh
make
sudo deploy/install.sh
```

The installer:

1. requires root (or `sudo`);
2. creates the `elektrodump` system user/group if missing;
3. installs the binary to `/opt/elektrodump/elektrodump` (mode 755);
4. installs `resources/` to `/opt/elektrodump/resources/`;
5. installs the unit to `/etc/systemd/system/`;
6. installs the tmpfiles entry to `/etc/tmpfiles.d/` and applies it;
7. seeds `/etc/elektrodump/config.json` from `config/config.example.json`
   **only if it does not already exist**;
8. runs `systemctl daemon-reload && systemctl enable --now elektrodump`;
9. prints the web URL and the log path.

## Upgrade

Re-run the installer. It overwrites the binary, resources, unit and tmpfiles
entry, but **keeps** the existing `/etc/elektrodump/config.json`:

```sh
make deploy-pi PI_HOST=pi@elektro.local
```

## Staged install (no root)

For testing or packaging, set `DESTDIR` to a staging prefix. The installer then
lays out every file under that prefix and skips user creation, `systemd-tmpfiles`
and `systemctl`:

```sh
DESTDIR=/tmp/stage deploy/install.sh
```

## Uninstall

```sh
sudo systemctl disable --now elektrodump
sudo rm -f /etc/systemd/system/elektrodump.service
sudo rm -f /etc/tmpfiles.d/elektrodump.conf
sudo systemctl daemon-reload
sudo rm -rf /opt/elektrodump
sudo rm -rf /etc/elektrodump
sudo rm -rf /var/lib/elektrodump
sudo userdel elektrodump
sudo groupdel elektrodump
```

## Operations

```sh
systemctl status elektrodump
journalctl -u elektrodump -f
```

The web UI listens on `web.port` (default `8080`) at `http://<host>:8080/`.
