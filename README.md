# ElektroDump

A headless, single-binary ground station for the Russian **Elektro-L No.3 / No.4**
geostationary weather satellites. It receives GGAK space-weather telemetry 24/7,
captures scheduled LRIT/HRIT full-disk images, bakes visual / IR / IR-cloud
composites, reprojects them to Web Mercator, and serves them through a local web
UI + REST API.

> Note: This codebase is entirely LLM-generated as this isnt something I'm comfortable dealing with.

Target: **Raspberry Pi 3B+ (Debian/Raspbian Lite)**. Build host: **WSL Debian**.
Build system: a plain **GNU Makefile** (no CMake).

## Usage

The complete, copy-pasteable guide lives in
[`docs/USAGE.md`](docs/USAGE.md) — requirements and hardware notes, build and
cross-compile, a hardware-free first run with the `mock` SDR, the full config
reference, SDR setup, operating modes, offline `decode`/`image`/`ggak decode`,
image products, the web UI + REST API, the scheduler, deployment and
troubleshooting.

## Build

```sh
# Release (default) -> build/elektrodump
make

# Debug (-O0 -g -fsanitize=address,undefined) -> build/elektrodump-debug
make debug

# Show the effective compiler flags + detected optional deps
make config

# Build + run the self-tests (one binary per tests/*.cpp)
make test

# Build + run the M2–M10 smoke scripts
make smoke

# Clean
make clean
```

Cross-compile for a 64-bit Pi:

```sh
make CROSS=aarch64-linux-gnu- PI=1
```

`-march=native` is deliberately never used; `PI=1` pins `-mcpu=cortex-a53`.

Install / deploy:

```sh
# Install the binary + resources under /usr/local (DESTDIR honoured)
make install

# Cross-build and push to a Pi, then run deploy/install.sh there
make deploy-pi PI_HOST=pi@elektro.local
make deploy-pi-cross PI_HOST=pi@elektro.local
```

Optional dependencies (librtlsdr, libairspy, FFTW3f, libmicrohttpd, libpng,
libwebp, libcurl) are detected with `pkg-config`; a missing dev library simply
disables that backend, so the project always builds.

## Run

```sh
./build/elektrodump --help
./build/elektrodump version
./build/elektrodump config validate
./build/elektrodump config dump
```

Commands:

| Command | Description |
|---|---|
| `run` | daemon: SDR + FFT/waterfall + active pipeline (GGAK/LRIT/HRIT) + scheduler + web + webhook |
| `serve` | web server only (offline browsing; no SDR) |
| `decode <dir>` | offline LRIT/HRIT decode (`.cadu` / `.cf32` / `.cu8`); `--mode lrit\|hrit` |
| `ggak decode <dir>` | offline GGAK decode (`.cadu` / `.cf32` / `.cu8`) to `ggak.csv` |
| `image <xrit-dir> [out-dir]` | assemble xRIT files into image products |
| `schedule list` | print the schedule and timezone |
| `schedule add ...` | add an entry (`--name --start --end --mode [--priority --frequency --symbol-rate]`) |
| `schedule remove <name>` | remove an entry |
| `schedule enable\|disable` | toggle the schedule |
| `probe` | list SDR devices and capabilities |
| `config validate` | validate the effective config |
| `config dump` | print the effective config |
| `version`, `help [command]` | version / help |

Common options: `-c/--config <path>`, `--mode <mode>` (`ggak` | `lrit` | `hrit`),
`-v/--verbose` (repeatable), `-q/--quiet`, `--log-json`, `-h/--help`, `--version`.

SDR drivers (`sdr.driver`): `rtlsdr`, `airspy`, `mock` (hardware-free synthetic
signal) and `file` (replay an IQ capture).

## LRIT/HRIT decode chain

The xRIT chain turns IQ into assembled xRIT files:

```
IQ -> BPSK/OQPSK demod (RRC + Costas + Gardner timing [+ coarse acquisition])
   -> Viterbi 1/2 (K=7) -> CCSDS deframer (ASM 0x1ACFFC1D) -> PN derandomiser
   -> Reed-Solomon rs223 (i=4) -> VCDU parse (per VCID) -> AOS demultiplex
   (per APID) -> xRIT file assembly + CRC-16
```

- `run.mode` selects the active pipeline (`ggak` default, or `lrit`/`hrit`); the
  time-of-day scheduler switches it automatically.
- `decode <dir>` runs the same chain offline over a directory of CADUs or IQ;
  `--mode hrit` selects the OQPSK + coarse-acquisition path.
- Assembled files are written to `lrit.output_dir` / `hrit.output_dir`
  (default `data/xrit`).

```sh
# Offline: derandomise + RS-correct CADUs and assemble xRIT files
./build/elektrodump decode some/cadu/dir

# Live, hardware-free: a synthetic RRC-shaped LRIT BPSK signal is generated
# and decoded end-to-end (see tests/m4_smoke.sh)
cat > /tmp/lrit.json <<'JSON'
{ "version":1, "run":{"mode":"lrit"},
  "sdr":{"driver":"mock","mock":{"lrit":true}},
  "lrit":{"output_dir":"data/xrit"} }
JSON
./build/elektrodump run -c /tmp/lrit.json
```

Hardware-free verification: `make smoke` (synthetic fixtures + mock daemons for
M2–M10) and `make test` (golden vectors for Viterbi, deframer, derandomiser,
Reed-Solomon, CRC-16, plus the full demod -> xRIT chain).

> Note: the CCSDS Reed-Solomon dual-basis byte transform is derived from the
> field trace and is disabled by default (`decode.rs_dualbasis`). It should be
> validated against a real capture before enabling on hardware.

## Configuration

One JSON file, embedded defaults, user override, validated and versioned.

- Defaults live in [`resources/default_config.json`](resources/default_config.json)
  and are compiled into the binary at build time.
- A user file (default `data/config.json`) is deep-merged over the defaults.
- `config.version` is an integer; migrations are explicit functions.
- `config validate` reports field-level errors and warnings; `config dump`
  prints the effective config.

See [`config/config.example.json`](config/config.example.json) for the full
schema and [`docs/PLAN.md`](docs/PLAN.md) section D10 for the rules.

## Layout

```
src/
  main.cpp          entry point
  cli/              argument parsing + one file per subcommand
  core/             app orchestrator, config, pipeline interface,
                    logging, metrics, time/thread utilities
  util/             json, fs, string, stats helpers
  sdr/ dsp/ fec/ ccsds/ xrit/ ggak/ image/ projection/ output/
  scheduler/ web/   scheduler + CADU lifecycle, web UI + REST + webhook
resources/          embedded defaults, LUTs, maps, calibration
config/             documented example config
web/                static web UI (embedded at build time)
deploy/             systemd unit + installer
tests/              self-tests, golden vectors and M2–M10 smoke scripts
tools/              build-time generators (web assets, M6 resources)
docs/               PLAN.md, REFERENCE_DIGEST.md, M6-M10-spec.md
```

## License

GPLv3. See [`LICENSE`](LICENSE) and [`ATTRIBUTION.md`](ATTRIBUTION.md).
