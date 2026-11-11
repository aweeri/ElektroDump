# ElektroDump — Usage Guide

A complete, copy-pasteable guide to building, configuring and operating
ElektroDump. Every command and output in this guide was captured from a real
run on WSL Debian (g++ 14.2.0, GNU Make 4.4.1). Where a step behaved
unexpectedly, it is recorded verbatim under
[Known issues](#15-troubleshooting).

> Design background lives in [`docs/PLAN.md`](PLAN.md) and the M6–M10 design
> record in [`docs/M6-M10-spec.md`](M6-M10-spec.md). Build/deploy specifics are
> in [`deploy/README.md`](../deploy/README.md).

---

## 1. What it is

ElektroDump is a single-binary, headless, always-on **ground station for the
Russian Elektro-L No.3 / No.4 geostationary weather satellites**. It drives an
RTL-SDR or Airspy at a fixed dish (the satellite is GEO — no tracking), and:

- receives **GGAK space-weather telemetry 24/7** at **1693 MHz** (PM/BPSK,
  5 kbaud), emitting 224-byte CADU frames and parsing SKL science records;
- on a **time-of-day schedule**, retunes to **LRIT** (1691 MHz, BPSK 294 kbaud)
  or **HRIT** (1691 MHz, OQPSK 1.15 Mbaud) and captures a **full-disk** pass;
- decodes the xRIT chain (Viterbi → CCSDS deframe → derandomise →
  Reed-Solomon → VCDU/AOS demux → xRIT assembly) into **image products**:
  `visual`, `ir` and `ir_cloud`;
- reprojects products to **Web Mercator** and stores them (WebP when available);
- serves a **local web UI + REST API** and fires an optional **HTTP POST
  webhook** when a new product is produced.

Everything is one process and one binary. `run` embeds the HTTP server; there is
no separate web daemon.

---

## 2. Requirements

**Host OS:** Debian / Raspbian (or WSL Debian). The project is POSIX-only and is
not supported on native Windows (use WSL or MSYS).

**Build essentials:**

```sh
sudo apt-get update
sudo apt-get install -y build-essential pkg-config
```

**Optional development libraries** — each is detected with `pkg-config`. A
missing dev library simply **disables that backend**; the project always
builds. What each one enables:

| Package (Debian) | Enables | If missing |
|---|---|---|
| `librtlsdr-dev` | RTL-SDR SDR backend (`-DED_HAVE_RTLSDR`) | `sdr.driver: "rtlsdr"` unavailable |
| `libairspy-dev` | Airspy SDR backend (`-DED_HAVE_AIRSPY`) | `sdr.driver: "airspy"` unavailable |
| `libfftw3-dev` | FFTW3f for the FFT/waterfall (`-DED_HAVE_FFTW3`) | built-in FFT used (slower) |
| `libmicrohttpd-dev` | the HTTP server / web UI + REST (`-DED_HAVE_MHD`) | **no web server** (build still succeeds) |
| `libpng-dev` | PNG LUT/map loading + PNG encode (`-DED_HAVE_PNG`) | PNG LUTs/maps not loaded; products use a built-in palette |
| `libwebp-dev` | WebP image encoding (`-DED_HAVE_WEBP`) | falls back to PNG (if libpng) or PPM |
| `libcurl4-openssl-dev` (a.k.a. `libcurl-dev`) | webhook delivery (`-DED_HAVE_CURL`) | webhook delivery disabled |

Install them all with:

```sh
sudo apt-get install -y librtlsdr-dev libairspy-dev libfftw3-dev \
    libmicrohttpd-dev libpng-dev libwebp-dev libcurl4-openssl-dev
```

**Hardware notes:**

- The Elektro-L downlinks are at **1691/1693 MHz**, far above an RTL-SDR's
  native range, so a **downconverter / LNB** is required, and **bias tee must be
  enabled** (`sdr.bias_tee: true`) to power it.
- **HRIT needs an Airspy-class rate.** The 1.15 Mbaud OQPSK needs ~6 MSPS,
  above the RTL-SDR's 3.2 MSPS ceiling, so HRIT requires `sdr.driver:
  "airspy"` (6 MSPS default). LRIT (1 MSPS) and GGAK (3 MSPS) work on RTL-SDR.
- No hardware is needed to evaluate the software: the `mock` and `file` SDR
  backends are hardware-free (see §4).

---

## 3. Build

```sh
# Release (default) -> build/elektrodump
make

# Debug (-O0 -g -fsanitize=address,undefined) -> build/elektrodump-debug
make debug

# Show the effective compiler flags + detected optional deps
make config

# Build + run the self-tests
make test

# Build + run the M2–M10 smoke scripts
make smoke

# Clean
make clean
```

`make config` prints the toggled backends. On a host with only the mandatory
packages, e.g.:

```
deps      = rtlsdr=1 airspy=1 fftw3f=1 microhttpd=1 png= webp= curl=1
```

A blank value (`png=`, `webp=`) means the dev library was **not** found, so that
backend is off. This particular build therefore writes **PPM** products instead
of WebP.

**Cross-compile for a 64-bit Raspberry Pi:**

```sh
make CROSS=aarch64-linux-gnu- PI=1
```

`-march=native` is deliberately never used; `PI=1` pins `-mcpu=cortex-a53`.
To cross-build and push in one step, see §13.

---

## 4. First run with NO hardware

The `mock` SDR synthesises a valid signal so the whole pipeline runs without a
radio. It has three modes, selected by boolean keys under `sdr.mock`:

- `sdr.mock.ggak: true` — synthetic PM/BPSK carrying valid 224-byte GGAK frames
- `sdr.mock.lrit: true` — RRC-shaped BPSK carrying a valid LRIT capture
- `sdr.mock.hrit: true` — RRC-shaped OQPSK carrying a valid HRIT capture

Create a config and start the daemon:

```sh
cat > /tmp/mock.json <<'JSON'
{
  "version": 1,
  "sdr": { "driver": "mock", "mock": { "ggak": true } },
  "ggak": { "spool_dir": "data/spool/ggak" },
  "web": { "bind": "127.0.0.1", "port": 8080 }
}
JSON

./build/elektrodump run -c /tmp/mock.json
```

Open `http://127.0.0.1:8080/` in a browser, or query the API:

```sh
curl -s http://127.0.0.1:8080/api/v1/status
curl -s http://127.0.0.1:8080/api/v1/signal
```

Real captured output (abridged):

```json
{"version":"0.1.0","uptime_seconds":3,"satellite":"elektro-l3","mode":"ggak",
 "sdr":{"available":true,"driver":"mock","name":"Synthetic IQ (mock)",
        "serial":"mock-0","center_freq_hz":1693000000,"sample_rate_hz":3000000,
        "gain_tenth_db":300,"bias_tee":true,"overflows":0,"samples":9617408},
 "pipeline":{"state":"running","locked":true,"ber":5.48e-48,
             "snr_db":20.22,"cadus_total":7,"cadus_bad":0,
             "files_total":0,"files_recovered":0,"iq_samples":9617408},
 "schedule":{"enabled":true,"active_entry":null,"next_entry":"lrit-morning",
             "next_transition_utc":"2026-10-27T06:00:00Z",
             "seconds_to_next":37961},
 "waterfall":{"fft_size":1024,"rate_hz":10,"frames":32,"running":true},
 "latest_products":null,"timestamp":"2026-10-26T19:27:20Z"}
```

> The `mock` backend emits no image products, so `/api/v1/products/latest`
> correctly returns `404 {"error":"no_product"}` until a real LRIT/HRIT capture
> (or the `image` command) has produced something.

Stop the daemon with Ctrl-C — see the **[known shutdown issue](#known-issues)**.

---

## 5. Configuration

One JSON file. **Embedded defaults** live in
[`resources/default_config.json`](../resources/default_config.json) and are
compiled into the binary at build time. A **user file** (default
`data/config.json`, override with `-c/--config`) is **deep-merged** over the
defaults, so a user file only needs the keys it changes. `config.version` is an
integer.

```sh
./build/elektrodump config validate          # field-level errors/warnings
./build/elektrodump config dump              # the effective (merged) config
./build/elektrodump config validate -c my.json
```

Real output:

```
$ ./build/elektrodump config validate
warning: config file 'data/config.json' not found; using embedded defaults
warning: products.ir_cloud.map: 'resources/maps/world_equirect.png' does not exist (map underlay disabled)
warning: storage.root: '/var/lib/elektrodump' does not exist yet (will be created)
config OK (embedded defaults)
```

`config dump` prints the fully merged config (all defaults plus your overrides).

> **Note:** the old `--data-dir` flag was removed; the config path is always
> selected with `-c/--config` (default `data/config.json`).

### Main key groups

| Group | Key fields | Meaning |
|---|---|---|
| `station` | `name`, `latitude`, `longitude`, `altitude_m` | Site metadata (used in sidecars). |
| `satellite` | `id` (e.g. `elektro-l3`), `subsatellite_lon_deg` | Target satellite. |
| `sdr` | `driver` (`rtlsdr`\|`airspy`\|`mock`\|`file`), `serial`, `gain_tenth_db`, `bias_tee`, `ppm`, `airspy{lna_gain,mixer_gain,vga_gain}`, `file_path`, `file_format`, `mock{...}` | Radio selection and gain. |
| `run.mode` | `ggak`\|`lrit`\|`hrit` | Active pipeline at startup (scheduler can switch it). |
| `frequencies` | `ggak_hz`, `lrit_hz`, `hrit_hz`, `lo_hz`, `*_if_hz` | Downlink center frequencies. |
| `ggak` | `sample_rate_hz`, `symbol_rate`, `pll_bw`, `rrc_alpha`, `costas_bw`, `subcarrier_hz`, `subcarrier_enabled`, `spool_dir`, `rotate_seconds`, `time_offset_seconds`, `mock{...}` | GGAK demod/spool. |
| `lrit` | `sample_rate_hz`, `symbol_rate`, `constellation`, `pll_bw`, `rrc_alpha`, `max_sps`, `output_dir`, `mock{...}` | LRIT demod/output. |
| `hrit` | same as `lrit` plus `coarse_acq`, `coarse_acq_range_hz`, `coarse_acq_fft` | HRIT OQPSK demod/output. |
| `decode` | `cadu_bits`, `derandomize`, `rs_interleave`, `rs_type`, `rs_usecheck`, `rs_dualbasis`, `viterbi_ber_threshold`, `viterbi_outsync_after`, `max_missing_segment_ratio` | FEC/xRIT chain. |
| `image` | `channel_map{vis_r,vis_g,vis_b,ir}`, `segment_height`, `max_missing_segment_ratio` | Channel mapping/assembly. |
| `calibration` | `enabled`, `table` | MSU-GS calibration table. |
| `products` | `max_output_dimension`, `format`, `webp_quality`, `webp_method`, `products_days`, `queue_depth`, `visual{...}`, `ir{...}`, `ir_cloud{...}` | Product rendering. |
| `projection` | `type` (`mercator`), `output_width`, `output_height` | Reprojection. |
| `schedule` | `enabled`, `timezone`, `default_mode`, `entries[]` | Time-of-day capture plan. |
| `scheduler` | `tick_seconds`, `janitor_seconds` | Scheduler loop timing. |
| `web` | `bind`, `port`, `static_root` | HTTP server. |
| `api` | `base_url`, `webhooks[]` | Webhook targets. |
| `logging` | `level`, `file`, `max_size_mb`, `rotate_count` | Logging sink. |
| `storage` | `root`, `retention{products_days,ggak_days,failed_cadu_hours,logs_days}`, `min_free_mb` | Retention/space guard. |

See [`config/config.example.json`](../config/config.example.json) for the fully
commented example.

---

## 6. SDR setup

`probe` lists devices and capabilities and **works without a config file**:

```sh
./build/elektrodump probe
```

Real output on a hardware-free host:

```
SDR devices (2):
  [mock] Synthetic IQ (mock)  sn=mock-0
      sample rate: 250000..10000000 Hz
      bias tee:    no
      gains (0.1 dB): 0, 10, 20, ... 400
  [file] IQ file ()  sn=file-0
      sample rate: 250000..10000000 Hz
      bias tee:    no

Backends: rtlsdr=yes airspy=yes mock=yes file=yes
configured driver: airspy
```

- `Backends:` shows which drivers were compiled in (`=yes`/`=no`).
- Real radios appear as `[rtlsdr]` / `[airspy]` with their serial, sample-rate
  range, bias-tee support and the discrete gain steps.
- `configured driver:` echoes `sdr.driver` from the effective config.

**Driver choice** (`sdr.driver`):

| Driver | Use |
|---|---|
| `rtlsdr` | RTL-SDR. Fixed gain via `sdr.gain_tenth_db` (0.1 dB steps; nearest step chosen). Sample-rate whitelist 250k–3200k. |
| `airspy` | Airspy. Manual gain via `sdr.airspy.{lna_gain,mixer_gain,vga_gain}` (0–15). Rates 3M/6M/10M. |
| `mock` | Hardware-free synthetic signal (see §4). |
| `file` | Replay an IQ capture: `sdr.file_path`, `sdr.file_format` (`int8`/...). |

Key settings:

- `sdr.gain_tenth_db` — fixed gain in 0.1 dB units (e.g. `300` = 30.0 dB).
- `sdr.bias_tee` — must be `true` for the 1691/1693 MHz LNB/downconverter.
- `sdr.ppm` — frequency correction for the receiver's crystal.
- Sample rate is constrained by the mode (GGAK 3 MSPS, LRIT 1 MSPS, HRIT 6 MSPS)
  and by the device ceilings reported by `probe`.

---

## 7. Operating modes

Three pipelines, selected by `run.mode` and/or the scheduler:

| Mode | Frequency | Modulation | Rate | Notes |
|---|---|---|---|---|
| `ggak` | 1693 MHz | PM/BPSK | 5 kbaud | 24/7 space-weather telemetry; default. |
| `lrit` | 1691 MHz | BPSK | 294 kbaud | Full-disk LRIT images; works on RTL-SDR. |
| `hrit` | 1691 MHz | OQPSK | 1.15 Mbaud | Full-disk HRIT; needs Airspy + coarse acquisition. |

- `run.mode` sets the pipeline used at startup.
- When `schedule.enabled` is true, the scheduler switches the active pipeline at
  each scheduled transition, then reverts to `schedule.default_mode` (default
  `ggak`) outside the entries.
- **Time-of-day entries**: each `schedule.entries[]` is
  `{name, start, end, mode, priority, frequency_hz, symbol_rate, sample_rate_hz}`.
  `start`/`end` are `HH:MM` in `schedule.timezone` (default `UTC`). Entries may
  wrap past midnight; when entries overlap, the highest `priority` wins.
  `frequency_hz`/`symbol_rate`/`sample_rate_hz` of `0` mean "use the mode's
  configured values".

Example entry (from the defaults):

```json
{ "name": "lrit-morning", "start": "06:00", "end": "06:20",
  "mode": "lrit", "priority": 10, "frequency_hz": 0,
  "symbol_rate": 0, "sample_rate_hz": 0 }
```

---

## 8. Offline decode

Decode a directory of CADUs or IQ to xRIT files — no SDR needed:

```sh
./build/elektrodump decode <dir> [--mode hrit]
```

Inputs handled (recursively):

- `*.cadu` — 1024-byte CADUs; derandomised, Reed-Solomon corrected, then
  demultiplexed straight to xRIT (no demod).
- `*.cf32` / `*.cu8` — IQ; run through the demod + FEC chain (BPSK for LRIT,
  OQPSK + coarse acquisition for HRIT).

Mode/output:

- `--mode hrit` selects the OQPSK + coarse-acquisition path; otherwise
  `run.mode` from the config is used (LRIT path when it is not `hrit`).
- Assembled files land in `lrit.output_dir` / `hrit.output_dir`
  (default `data/xrit`); pass `-c` to override.

Real output (LRIT `.cadu` fixture):

```
$ ./build/elektrodump decode build/m4_fixture -c lrit_out.json
  /tmp/.../000000_SYNTH_3.lrit (3494 B)
decode (ggak): 1 file(s), 4 CADU, 1 xRIT file(s)
  viterbi BER 10, viterbi unlocked, deframer unlocked, RS corrected 0, uncorrectable 0
  demux: 1 file(s), vcdu filler 0
output: /tmp/...
```

Real output (HRIT `.cf32` fixture):

```
$ ./build/elektrodump decode build/m5_fixture --mode hrit -c hrit_out.json
  /tmp/.../000000_SYNTH_3.lrit (3494 B)
  /tmp/.../000001_SYNTH_3.lrit (3494 B)
  /tmp/.../000002_SYNTH_3.lrit (3494 B)
  build/m5_fixture/capture.cf32: SNR 21.8316 dB, carrier offset 24993.9 Hz
decode (hrit): 1 file(s), 15 CADU, 3 xRIT file(s)
  viterbi BER 6.99e-156, viterbi locked, deframer locked, RS corrected 0, uncorrectable 0
  demux: 3 file(s), vcdu filler 0
output: /tmp/...
```

> **Heads-up:** when decoding `.cadu` without `--mode`, the summary label echoes
> the raw `run.mode` (here `decode (ggak)`) even though the LRIT/xRIT path was
> used, and the `viterbi …` line is only meaningful for IQ input. See
> [Known issues](#known-issues).

---

## 9. Image products

Assemble `.lrit` segments in a directory into full-disk products:

```sh
./build/elektrodump image <xrit-dir> [out-dir]
```

- Reads `*.lrit` files recursively (skips a `products/` subdirectory).
- Stitches segments into full-disk **channels**, calibrates them
  (`calibration.table`), builds the **`visual`**, **`ir`** and **`ir_cloud`**
  composites, reprojects to `projection.type` (Mercator) and writes each
  product plus a `.json` metadata sidecar.
- Default output directory is `<xrit-dir>/products`.

Real output and product layout:

```
$ ./build/elektrodump image build/m6_fixture /tmp/out
image: 16 file(s), 4 channel(s), 3 product(s)
output: /tmp/out

/tmp/out/products/2026/10/26/elektro-l3/ir/20261026T193144Z_ir.json
/tmp/out/products/2026/10/26/elektro-l3/ir/20261026T193144Z_ir.ppm
/tmp/out/products/2026/10/26/elektro-l3/ir_cloud/20261026T193144Z_ir_cloud.json
/tmp/out/products/2026/10/26/elektro-l3/ir_cloud/20261026T193144Z_ir_cloud.ppm
/tmp/out/products/2026/10/26/elektro-l3/visual/20261026T193144Z_visual.json
/tmp/out/products/2026/10/26/elektro-l3/visual/20261026T193144Z_visual.ppm
/tmp/out/state/latest.json
```

Product types:

- **visual** — RGB from `products.visual.{r_channel,g_channel,b_channel}` with a
  gamma/stretch.
- **ir** — single IR channel through a 1-D LUT (`products.ir.lut`), mapped over
  `min_k..max_k`.
- **ir_cloud** — IR with a cloud palette and (optional) map underlay
  (`products.ir_cloud.map_enabled`).

Where products go: the product store writes under
`<storage.root or out-dir>/products/<YYYY>/<MM>/<DD>/<satellite>/<product>/`,
with a `.json` sidecar per image and `state/latest.json` tracking the newest of
each. Format follows `products.format` but falls back by availability:
WebP → PNG → PPM. **In this build (no libwebp/libpng) products are PPM.**
Projection size comes from `projection.output_width`/`output_height` and
`products.max_output_dimension`.

---

## 10. GGAK offline

Decode a GGAK spool (or any dir of `.cadu`/`.cf32`/`.cu8`) to a CSV:

```sh
./build/elektrodump ggak decode <dir>
```

- `.cadu` — already-demodulated 224-byte frames; validated (sum-CRC) directly.
- `.cf32` / `.cu8` — run through the PM demod + frame sync first.
- Writes `ggak.csv` **into the input directory** with header
  `utc,id,crc_ok,frame_time`.

Real output on a mock-produced spool:

```
$ ./build/elektrodump ggak decode /tmp/.../spool -c mock.json
ggak decode: 1 file(s), 20 frame(s), 20 valid, 0 invalid
CSV: /tmp/.../spool/ggak.csv

$ head -n 5 /tmp/.../spool/ggak.csv
utc,id,crc_ok,frame_time
2008-07-03T18:24:17Z,48,1,268435457
2008-07-03T18:24:18Z,48,1,268435458
2008-07-03T18:24:19Z,48,1,268435459
2008-07-03T18:24:20Z,48,1,268435460
```

### GGAK demodulation parameters

`ggak.subcarrier_hz` is the PM subcarrier derotated after the carrier loop; `0`
keeps the historical meaning "use `ggak.symbol_rate`". A **plain BPSK** link has
no subcarrier and has to say so explicitly, because `0` is already taken:

```json
{ "ggak": { "symbol_rate": 8000, "subcarrier_enabled": false } }
```

`ggak.pll_bw` is the carrier-loop bandwidth as a fraction of the *decimated*
sample rate. Decimation targets `ggak.target_sps` samples/symbol, so a 5 kbaud
signal at 3 MSPS decimates to 40 kHz and `pll_bw: 0.02` is an 800 Hz loop.
Very small values acquire slowly on a weak signal: a 6 s synthetic capture at
9 dB SNR recovered 15 of 17 frames at `pll_bw` 0.02 and 0.05 but only 2 at 0.005.

### Frame sync is confirmed, not single-shot

A GGAK frame is validated by a 16-bit **sum** of bytes 0..221, which random data
satisfies about once every 65 536 bit positions. Since acquisition slides one
bit at a time in both polarities, one match is not evidence of a frame: the
parser requires three consecutive matches at the same start before declaring
lock and only emits frames once confirmed. Without that check a signal-free
3 MSPS capture produced 2-10 "valid" frames per 30 s; with it, zero, while a
genuine 9 dB SNR capture still yields every frame.

**Live GGAK endpoints:** the running daemon serves the in-memory science store
at `/api/v1/ggak`, `/api/v1/ggak/plots`, `/api/v1/ggak/csv?channel=N` and
`/api/v1/ggak/plots/N.svg`. Real capture:

```json
{"available":true,"channels":[
  {"id":0,"name":"SKL-00","unit":"counts","present":true,"has_value":true,
   "latest":120277.3,"time":"2008-07-03T18:24:24Z","count":112},
  {"id":1,"name":"SKL-01","unit":"counts","present":true,"has_value":true,
   "latest":2070,"time":"2008-07-03T18:24:24Z","count":112},
  ...
  {"id":10,"name":"SKL-10","unit":"counts","present":false,"has_value":false,
   "latest":0,"time":"","count":0},
  {"id":11,"name":"SKL-11","unit":"counts","present":true,"has_value":true,
   "latest":2160,"time":"2008-07-03T18:24:24Z","count":112}],
 "stats":{"frames":8,"skl_frames":8,"records":112},
 "timestamp":"2026-10-26T19:27:20Z"}
```

> **Channel not yet populated:** `SKL-10` reports `present:false` /
> `has_value:false` (no value seen in the test capture). Empty series still
> render valid JSON/SVG with correct HTTP status codes rather than fabricated
> values.

---

## 11. Web UI & API

The multi-page UI is embedded in the binary at build time and served from the
root. Extensionless paths map to the matching `.html`:

| Page | URL | Purpose |
|---|---|---|
| Dashboard | `/` (`/index.html`) | Live status, signal, waterfall summary. |
| Gallery | `/gallery` | Product browsing (`/api/v1/products`). |
| Schedule | `/schedule` | View/edit the schedule (`/api/v1/schedule`). |
| GGAK | `/ggak` | Space-weather channels and plots. |
| System | `/system` | CPU/memory/disk/temperature health. |
| Waterfall | `/waterfall` | Live FFT/waterfall. |

### REST endpoints

| Method | Path | Notes |
|---|---|---|
| GET | `/api/v1/status` | Version, uptime, mode, SDR, pipeline, schedule, waterfall, latest products. |
| GET | `/api/v1/signal` | `snr_db`, `locked`, `ber`, SDR tuning. |
| GET | `/api/v1/health` | CPU %, memory, temperature, disk free/total, load. |
| GET | `/api/v1/config` | Effective config (secrets redacted). |
| GET | `/api/v1/fft` | Latest spectrum row (`bins`, `center_hz`, `span_hz`). |
| GET | `/api/v1/waterfall?rows=N` | Last N spectrum rows (N ≤ 256). |
| GET | `/api/v1/products?type=&date=&limit=` | List products (`limit` ≤ 1000, default 50). |
| GET | `/api/v1/products/latest?product=visual` | Newest product image bytes. |
| GET | `/api/v1/products/{id}` | Product metadata JSON. |
| GET | `/api/v1/products/{id}/image` | Product image bytes. |
| GET | `/api/v1/schedule` | Schedule + scheduler state. |
| PUT | `/api/v1/schedule` | Replace/apply the schedule (400 + field errors on failure). |
| GET | `/api/v1/ggak?channel=&limit=` | Channel list (or a single channel's series). |
| GET | `/api/v1/ggak/plots` | Available channel plots. |
| GET | `/api/v1/ggak/csv?channel=N` | CSV samples for a channel. |
| GET | `/api/v1/ggak/plots/{N}.svg` | Server-rendered SVG plot. |
| POST | `/api/v1/webhook/test` | Reports webhook configuration/`enabled`. |

Examples:

```sh
curl -s http://127.0.0.1:8080/api/v1/status
curl -s http://127.0.0.1:8080/api/v1/products/latest?product=ir -o ir.ppm
curl -s 'http://127.0.0.1:8080/api/v1/products?type=visual&limit=10'
curl -s http://127.0.0.1:8080/api/v1/ggak?channel=0
curl -s 'http://127.0.0.1:8080/api/v1/schedule'
```

Example schedule response:

```json
{"schedule":{"enabled":true,"timezone":"UTC","default_mode":"ggak",
  "entries":[{"name":"lrit-morning","start":"06:00","end":"06:20","mode":"lrit",
              "priority":10,"frequency_hz":0,"symbol_rate":0,"sample_rate_hz":0}]},
 "state":{"enabled":true,"active_entry":null,"next_entry":"lrit-morning",
          "next_transition_utc":"2026-10-27T06:00:00Z","seconds_to_next":37961}}
```

> `curl` is not installed by default in a bare WSL Debian; install it
> (`sudo apt-get install -y curl`) or use `python3 -c
> 'import urllib.request;print(urllib.request.urlopen("URL").read().decode())'`.

---

## 12. Scheduling

Edit the capture schedule from the CLI (persisted to the user config):

```sh
./build/elektrodump schedule list
./build/elektrodump schedule add --name demo --start 12:00 --end 12:20 \
    --mode hrit --priority 5
./build/elektrodump schedule remove demo
./build/elektrodump schedule enable
./build/elektrodump schedule disable
```

`add` flags: `--name`, `--start HH:MM`, `--end HH:MM`, `--mode lrit|hrit`
(required); `--priority N`, `--frequency HZ`, `--symbol-rate R` (optional).
Adding a name that already exists replaces that entry.

Exit codes: **0** success, **2** usage error, **1** validation/persistence
failure (field-level messages on stderr). Real sessions:

```
$ ./build/elektrodump schedule list -c demo.json
schedule: enabled  timezone: UTC  default: ggak
  name                 start  end    mode  prio  freq_hz     sym_rate  sample_hz
  lrit-morning         06:00  06:20  lrit    10  0           0          0
$ echo $?            # 0

$ ./build/elektrodump schedule add --name bad --start 99:99 --end 00:00 --mode lrit -c demo.json
error: --start: expected HH:MM
$ echo $?            # 2

$ ./build/elektrodump schedule remove nope -c demo.json
error: no such entry: nope
$ echo $?            # 1
```

The equivalent REST API is `GET /api/v1/schedule` and `PUT /api/v1/schedule`.
`PUT` accepts either the schedule object directly or `{"schedule": {...}}`,
validates it, applies it live, and returns `200` (new schedule + state) or `400`
with `{"error":"validation_failed","fields":[{"path","message"},...]}`.

---

## 13. Deployment

**Staged install (no root)** — lays out every file under `DESTDIR`:

```sh
make install DESTDIR=/tmp/stage
```

Real output:

```
install -Dm755 build/elektrodump /tmp/stage/usr/local/bin/elektrodump
install -d /tmp/stage/usr/local/share/elektrodump/resources
cp -R resources/. /tmp/stage/usr/local/share/elektrodump/resources/
```

**Raspberry Pi deploy** — `deploy/install.sh` installs the binary, resources,
systemd unit and tmpfiles entry, seeds `/etc/elektrodump/config.json` (never
clobbering an existing one) and enables the service:

```sh
# From the build host (cross-build first for a 64-bit Pi):
make CROSS=aarch64-linux-gnu- PI=1
make deploy-pi PI_HOST=pi@elektro.local

# Or cross-build + deploy in one step:
make deploy-pi-cross PI_HOST=pi@elektro.local

# On the Pi itself, from a checkout:
make
sudo deploy/install.sh
```

The systemd unit [`deploy/elektrodump.service`](../deploy/elektrodump.service)
runs `Type=simple` as user `elektrodump`, `WorkingDirectory=/opt/elektrodump`
(so relative resource paths resolve), `Restart=always`, and memory/priority
limits for a 1 GB Pi 3B+:

```
ExecStart=/opt/elektrodump/elektrodump run --config /etc/elektrodump/config.json
```

Installed layout: binary at `/opt/elektrodump/elektrodump`, resources at
`/opt/elektrodump/resources/`, config at `/etc/elektrodump/config.json`, unit at
`/etc/systemd/system/elektrodump.service`, tmpfiles at
`/etc/tmpfiles.d/elektrodump.conf` (`/var/lib/elektrodump` = `storage.root`).

Operations:

```sh
systemctl status elektrodump
journalctl -u elektrodump -f
```

> SIGTERM was re-verified on a Raspberry Pi 4B: the daemon logs `stop requested;
> shutting down` then `shutdown complete` and exits in well under a second, web
> server included. If a stop does hang on another host, capture the thread
> stacks before reaching for `TimeoutStopSec=5`.

---

## 14. Tests & verification

```sh
make test     # build + run every tests/*.cpp self-test
make smoke    # build + run the M2–M10 smoke scripts
```

**`make test`** builds one binary per `tests/*.cpp` and runs them. On the
verified build (15 binaries, all pass):

```
--- build/elektrodump_test_test_cadu_lifecycle-release   all M8 CADU lifecycle tests passed
--- build/elektrodump_test_test_calibration-release      all M6b calibration tests passed
--- build/elektrodump_test_test_composite-release        all M6c composite tests passed
--- build/elektrodump_test_test_image_assembler-release  all M6a image assembler tests passed
--- build/elektrodump_test_test_m1-release               all M1 tests passed
--- build/elektrodump_test_test_m2-release               all M2 tests passed
--- build/elektrodump_test_test_m3-release               all M3 tests passed
--- build/elektrodump_test_test_m4-release               all M4 tests passed
--- build/elektrodump_test_test_m5-release               all M5 tests passed
--- build/elektrodump_test_test_product_store-release    all M7b product store tests passed
--- build/elektrodump_test_test_projection-release       all M7a projection tests passed
--- build/elektrodump_test_test_schedule_model-release   all M8 schedule-model tests passed
--- build/elektrodump_test_test_scheduler-release        all M8 scheduler tests passed
--- build/elektrodump_test_test_webhook-release          test_webhook: PASS
--- build/elektrodump_test_test_webp-release             all M7b webp tests passed (format: ppm)
```

These cover golden vectors for CRC-16, the PN derandomiser, Viterbi, the CCSDS
deframer, Reed-Solomon, and the full demod → xRIT chain, plus calibration,
composites, projection, product store, WebP/PPM, scheduling and webhook.

**`make smoke`** runs the hardware-free M2–M10 scripts:

| Script | Covers |
|---|---|
| `m2_smoke.sh` | Mock SDR daemon + status/signal/fft/waterfall/config API and 404s. |
| `m3_smoke.sh` | Mock GGAK: lock, SNR, CADU spool, `ggak decode` + `ggak.csv`. |
| `m4_smoke.sh` | Offline LRIT decode of a synthetic CADU fixture + mock LRIT daemon. |
| `m5_smoke.sh` | Offline HRIT decode of a synthetic IQ fixture + mock HRIT daemon. |
| `m6_smoke.sh` | Image assembly/calibration/composites + RSS budget. |
| `m7_smoke.sh` | Mercator reprojection, product store, `/api/v1/products*`. |
| `m8_smoke.sh` | Schedule CLI round-trip, schedule REST API, live mode switch. |
| `m9_smoke.sh` | Embedded web assets + GGAK/webhook API, live GGAK store, webhook delivery. |
| `m10_smoke.sh` | Unit/tmpfiles sanity, `DESTDIR` staged install, idempotent upgrade. |

Observed result — `M3..M10 smoke: PASS` on every script (`m2_smoke.sh` prints
API payloads rather than a PASS line; it exits 0). `make smoke` itself returned
exit 0.

---

## 15. Troubleshooting

### Known issues

- **`decode` summary mode label.** Decoding `.cadu` without `--mode` prints
  `decode (ggak)` (the raw `run.mode`) even though the LRIT/xRIT path is used,
  and the `viterbi …` line is only meaningful for IQ input. Cosmetic.

- **GGAK channel `SKL-10` not populated.** In the verified capture SKL-10
  reported `present:false`/`has_value:false`. The endpoints return well-formed
  empty results for it rather than erroring.

- **`products.ir_cloud.map` default path missing.** The default points at
  `resources/maps/world_equirect.png`, which ships as an empty `.gitkeep` dir,
  producing the config warning `map underlay disabled`. Set `map_enabled: false`
  (the default) or supply a real map.

- **`storage.root` default missing.** `config validate` warns
  `storage.root: '/var/lib/elektrodump' does not exist yet (will be created)` on
  a dev host; this is expected and is created by the tmpfiles entry on a Pi.

### "No SDR detected"

`probe` shows only `mock`/`file`, or `Backends: rtlsdr=no`. Check the dev
library was present at build time (`make config` -> `rtlsdr=1`), the device is
plugged in, and permissions allow it. `probe` works without a config file.

### The device is plugged in but `probe` lists no radio

The `libairspy`/`librtlsdr` udev rules only tag the device with
`TAG+="uaccess"`, which grants access to the **logged-in seat user**. A systemd
system service has no seat, so `User=elektrodump` cannot open the radio and the
daemon logs `airspy_open failed (device busy or absent)`.

`deploy/install.sh` handles this: it installs
[`deploy/99-elektrodump-sdr.rules`](../deploy/99-elektrodump-sdr.rules), which
makes the nodes group-owned by `plugdev` with mode `0660`, and adds the service
account to `plugdev` (`SupplementaryGroups=plugdev` in the unit). For a manual
install, do the same by hand:

```sh
sudo install -m 0644 deploy/99-elektrodump-sdr.rules /etc/udev/rules.d/
sudo usermod -a -G plugdev elektrodump
sudo udevadm control --reload-rules && sudo udevadm trigger --action=add
ls -l /dev/bus/usb/*/   # the node should now be root:plugdev 0660
```

Re-running `udevadm trigger` matters even without a custom rule: a node that was
enumerated before the `libairspy` package rules landed keeps its original
`root:root 0664` ownership, so a non-root user cannot open the device until it is
re-triggered or re-plugged.

### Config validation errors

`config validate` prints `error: <path>: <message>` per field and exits non-zero.
Fix the named key; `config dump` shows the merged result. Unknown keys are
preserved but ignored.

### Unreadable xRIT CRC

A file that fails CRC is either skipped or written with `crc-recovered` (the
CRC-16 is repaired from the frame). Repeated failures usually mean a weak
signal — check `snr_db`/`locked` in `/api/v1/status`, LNB bias-tee, and gain.

### "Pipeline not locked"

`locked:false` in `/api/v1/status` or `/api/v1/signal` means the demod hasn't
acquired. Confirm the correct `run.mode`/frequency and sample rate, satellite
tuning and dish pointing, and that HRIT is not being attempted on an RTL-SDR.

### Missing optional libraries

If `make config` shows `microhttpd=` the build has **no web server** — install
`libmicrohttpd-dev` and rebuild. `webp=`/`png=` mean WebP/PNG are unavailable;
products fall back to PPM. `curl=` disables webhook delivery.

### Where logs go

- `logging.file` (default `data/logs/elektrodump.log`, rotated by
  `max_size_mb`/`rotate_count`), and everything is also written to stderr.
- Under systemd: `journalctl -u elektrodump -f`.
- Increase verbosity with `-v` (repeat for trace) or use `--log-json` for
  structured output; `-q` shows errors only.

---

## 16. Legal

ElektroDump is licensed **GPLv3** — see [`LICENSE`](../LICENSE). It is a
clean-room reimplementation that reuses published protocol facts and methods
(CCSDS, EUMETSAT LRIT/HRIT, Elektro-L GGAK) and credits SatDump/GobDump as
methodological references only. See [`ATTRIBUTION.md`](../ATTRIBUTION.md) for
the full list of references and third-party library licenses.
