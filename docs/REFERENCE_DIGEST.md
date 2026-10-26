# Reference Digest — GobDump_reference (SatDump-derived)

> Scope: deep technical digest of `GobDump_reference/` for the ElektroDump reimplementation.
> Goal: extract **methods/algorithms** only. We are **not** copying organization or code.
> Every subsystem below documents: what it does, key files/functions, core algorithm, external deps,
> and a **STEAL THIS / DISCARD THIS** verdict.

---

## 0. Executive orientation

GobDump is a fork of **SatDump 2.0-alpha** (GPLv3). The useful material for ElektroDump is concentrated in a
small fraction of the tree:

| Concern | Where it lives | Value to us |
|---|---|---|
| Elektro-L / Arktika pipelines | [`resources/pipelines/Elektro_Arktika.json`](../GobDump_reference/resources/pipelines/Elektro_Arktika.json) | **Priceless** — exact demod/decoder parameters |
| Elektro-L LRIT demux + products | [`plugins/elektro_arktika_support/`](../GobDump_reference/plugins/elektro_arktika_support) + [`plugins/xrit_support/`](../GobDump_reference/plugins/xrit_support) | **High** — xRIT chain, MSU-GS readers |
| CCSDS / Viterbi / RS / CADU | [`src-core/common/codings/`](../GobDump_reference/src-core/common/codings) + [`src-core/common/ccsds/`](../GobDump_reference/src-core/common/ccsds) | **High** — the actual error-correction math |
| SDR abstraction | [`plugins/sdr_sources/`](../GobDump_reference/plugins/sdr_sources) | **Medium** — thin lib callbacks |
| Image products / composites | [`src-core/products/`](../GobDump_reference/src-core/products) + [`src-core/image/`](../GobDump_reference/src-core/image) | **Medium-High** — expression engine, alpha |
| Projection | [`src-core/projection/`](../GobDump_reference/src-core/projection) | **Medium** — self-contained, no PROJ dep |
| Webserver / scheduler | [`src-cli/legacy/webserver.cpp`](../GobDump_reference/src-cli/legacy/webserver.cpp), [`src-core/common/tracking/scheduler/`](../GobDump_reference/src-core/common/tracking/scheduler) | **Low-Medium** — nng-based, GUI-coupled |
| Build system | [`CMakeLists.txt`](../GobDump_reference/CMakeLists.txt), [`Makefile`](../GobDump_reference/Makefile) | **Discard** — we use a plain Makefile |

**Architecture verdict:** GobDump's global design is `shared core library + dlopen plugins + ImGui GUI + CLI`,
with a heavy JSON-"pipeline descriptor" system, an EventBus, and an optional scripting layer. It is
over-engineered for a headless Pi appliance. We keep the *math*, drop the *plumbing*.

---

## 1. Build system & dependencies

### 1.1 What it is
- Top-level [`CMakeLists.txt`](../GobDump_reference/CMakeLists.txt) (537 lines): project `GobDump` v2.0.0-alpha, C17/C++17.
- [`Makefile`](../GobDump_reference/Makefile): a thin wrapper that just calls `cmake -S . -B build -G Ninja -DPLUGINS_ALL=ON`.
- Dependency lists: [`packages.builder`](../GobDump_reference/packages.builder) (build-time), [`packages.runner`](../GobDump_reference/packages.runner) (runtime).
- [`src-core/CMakeLists.txt`](../GobDump_reference/src-core/CMakeLists.txt) builds `gobdump_core` (SHARED) by **globbing** `*.cpp`
  (`file(GLOB_RECURSE …)` — fragile, and the reason they have `distclean`/`copy-compile-commands` hacks).
- [`plugins/CMakeLists.txt`](../GobDump_reference/plugins/CMakeLists.txt): 550 lines of per-satellite `option(PLUGIN_*)` toggles, each `add_subdirectory`.

### 1.2 How third-party code is pulled in
1. **System packages** via `find_package(...)` / `pkg_check_modules(...)` (see list below).
2. **Vendored C/C++ source trees** under [`src-core/libs/`](../GobDump_reference/src-core/libs) and
   [`plugins/xrit_support/DecompWT/`](../GobDump_reference/plugins/xrit_support/DecompWT) — compiled directly into the target.
3. **Single-header libs** under [`src-core/libs/`](../GobDump_reference/src-core/libs) (`CLI11.hpp`, `qoi.h`, `rapidxml.hpp`, `ctpl_stl.h`).
4. **Lua** vendored under `src-core/libs/lua`.
5. **[`volk_includes/`](../GobDump_reference/volk_includes)** fallback shim for old VOLK.

### 1.3 FULL dependency list

**A. System packages ([`packages.builder`](../GobDump_reference/packages.builder))**
```
build-essential cmake g++ git pkgconf
libairspy-dev libairspyhf-dev libbladerf-dev libboost-dev
libcurl4-openssl-dev libdbus-1-dev libfftw3-dev libglfw3-dev
libhackrf-dev libhdf5-dev libiio-dev libjemalloc-dev liblimesuite-dev
libnng-dev libomp-dev libpng-dev librtlsdr-dev libsqlite3-dev libtiff-dev
libuhd-dev libvolk-dev libxrandr-dev libzstd-dev
ocl-icd-opencl-dev portaudio19-dev libad9361-dev
```
**B. Runtime packages ([`packages.runner`](../GobDump_reference/packages.runner))**
```
libad9361-0 libairspy0 libairspyhf1 libbladerf2 libdbus-1-3 libfftw3-bin
libglfw3 libhackrf0 libhdf5-310 libiio0 libjemalloc2 liblimesuite23.11-1
libnng1 libomp5-19 libopengl0 libpng16-16 libportaudiocpp0 librtlsdr0
libsqlite3-0 libtiff6 libuhd4.8.0 libvolk3.2 libxrandr2 libzstd1
ocl-icd-libopencl1 uhd-host zenity
```
**C. `find_package` / `pkg_check_modules` (from CMake sources)**
- Top-level: `Jemalloc`, `ZSTD`, `OpenCL`, `Doxygen`.
- [`src-core/CMakeLists.txt`](../GobDump_reference/src-core/CMakeLists.txt): `Threads`, `Armadillo` (optional),
  `Jemalloc`, `VOLK` (via pkg-config), `PNG`, `FFTW3` (`fftw3f`, pkg-config), `NNG` (find_library),
  `ZSTD` (find_library), `CURL`, `SQLite3`, `TIFF` (find_library), `dl`.
- GUI: `OpenGL`/`OpenGLES2`, `glfw3`, GTK3 or D-Bus (nfd).
- Plugins: `HDF5`, `ZLIB`, `libusb-1.0`, `LibHTRA` (Harogic), `libctpl` (header only).

**D. Vendored source trees** (compiled in, no system dep)
- [`src-core/libs/`](../GobDump_reference/src-core/libs): `aec` (libaec — CCSDS/AOS lossless),
  `bpe` (CCSDS 122.0-B image compression: wavelet + bit-plane + Rice), `jpeg`, `jpeg12` (12-bit JPEG),
  `openjp2` (OpenJPEG), `bzlib`, `miniz`, `libtom` (libtomcrypt — DES for GK2A),
  `correct` (libcorrect — FEC), `deepspace-turbo`, `tiny-regex-c`, `supernovas` (NOVAS astrometry),
  `calceph` (SPICE ephemeris), `mqttc`, `lua`, `muparser`, `base64`, `ctpl`, `dlfcn`, `rapidxml`, `CLI11`, `qoi`.
- [`plugins/xrit_support/DecompWT/`](../GobDump_reference/plugins/xrit_support/DecompWT): EUMETSAT wavelet decompressor
  (CWTCoder/CWBlock/T4Coder/CVLCCoder — JPEG2000-like, used for MSG/SEVIRI & related).
- `src-core/angelscript/` (AngelScript scripting engine — **unused/experimental**).

### 1.4 STEAL / DISCARD
- **DISCARD:** CMake + globbing + plugin `dlopen` + EventBus + AngelScript + per-satellite plugin dirs.
  They add ~500-line CMake and fragile glob builds for zero benefit to a single-satellite appliance.
- **STEAL:** the *contents* of the dependency list. For a Pi 3B+ we need far less:
  `librtlsdr`, `libairspy`, `libvolk` (or scalar), `libfftw3f`, `libpng`, `libjpeg`, `zlib`,
  `nlohmann/json` (header-only), `libcurl`, `libsqlite3`, plus our own AES/CCSDS. **No OpenCL, no HDF5,
  no UHD, no SoapySDR, no Lua, no OpenGL, no GUI.**
- **NOTE:** `-march=native` is used on non-CI builds — **dangerous for cross/deploy**; we must pin
  `-mcpu=cortex-a53` / `-mfpu=neon` for the Pi and build in WSL/Debian.

---

## 2. SDR abstraction

### 2.1 What it does
Every SDR is a plugin implementing [`dsp::DSPSampleSource`](../GobDump_reference/src-core/common/dsp_source_sink/dsp_sample_source.h).
The base class exposes `open()/start()/stop()/close()`, `set/get_frequency`, `set/get_samplerate`,
`set/get_settings(json)`, `drawControlUI()` (ImGui). Samples are delivered into a
`std::shared_ptr<dsp::stream<complex_t>> output_stream` — a ring buffer of `complex_t` (2×float32).

### 2.2 Key files / functions
- Base: [`dsp_sample_source.h`](../GobDump_reference/src-core/common/dsp_source_sink/dsp_sample_source.h) — registry `dsp_sources_registry`, `SourceDescriptor{source_type,name,unique_id}`.
- RTL-SDR: [`plugins/sdr_sources/rtlsdr_sdr_support/rtlsdr_sdr.cpp`](../GobDump_reference/plugins/sdr_sources/rtlsdr_sdr_support/rtlsdr_sdr.cpp), `.h`.
- Airspy: [`plugins/sdr_sources/airspy_sdr_support/airspy_sdr.cpp`](../GobDump_reference/plugins/sdr_sources/airspy_sdr_support/airspy_sdr.cpp), `.h`.

### 2.3 Core algorithm / mechanics
**RTL-SDR** ([`rtlsdr_sdr.cpp`](../GobDump_reference/plugins/sdr_sources/rtlsdr_sdr_support/rtlsdr_sdr.cpp)):
- Async callback `_rx_callback(buf,len,ctx)`: converts `uint8` to `complex_t` with
  `real=(buf[i*2]-127.4)/128.0`, `imag=(buf[i*2+1]-127.4)/128.0`, then `stream->swap(len/2)`.
- `open()`: declares a **fixed samplerate whitelist** (250k…3.2M). `start()`: `rtlsdr_open` by serial,
  `rtlsdr_get_tuner_gains` → sorted `available_gains`, `rtlsdr_set_sample_rate`, then gain/bias/ppm, then
  `rtlsdr_reset_buffer` and a worker thread `mainThread` (async read loop).
- `set_gains()`: `lower_bound` picks nearest discrete gain step (gains are in tenths of dB); supports
  `lna_agc` (`rtlsdr_set_agc_mode`) and `tuner_agc` (`rtlsdr_set_tuner_gain_mode`).
- `set_bias()`: `rtlsdr_set_bias_tee`. `set_ppm()`: `rtlsdr_set_freq_correction`.
- **Surprise / gotcha to keep:** [`set_frequency()`](../GobDump_reference/plugins/sdr_sources/rtlsdr_sdr_support/rtlsdr_sdr.cpp:230) contains a
  hard-won workaround — **for f > 1 GHz the PLL may not lock on first tune**, so it tunes to `f-1e9` then back to `f`.
  This is directly relevant: **Elektro-L LRIT/HRIT is at 1691 MHz and GGAK at 1693 MHz** (both > 1 GHz), so with an
  LNB/downconverter this still matters if the *tuner* lands >1 GHz.
- All setters retry **up to 20 times** and log; sample-rate change is only allowed when stopped.

**Airspy** ([`airspy_sdr.cpp`](../GobDump_reference/plugins/sdr_sources/airspy_sdr_support/airspy_sdr.cpp)):
- Callback `_rx_callback(airspy_transfer*)`: `memcpy(stream->writeBuf, t->samples, count*sizeof(complex_t)); stream->swap(count)` — device already produces `FLOAT32_IQ`.
- `open_sdr()` via `airspy_open_sn` (serial). Gain modes: 0=sensitivity, 1=linearity, 2=manual(LNA/Mixer/VGA).
- `set_bias()` → `airspy_set_rf_bias`; `set_agcs()` → `airspy_set_lna_agc` + `airspy_set_mixer_agc`.

### 2.4 STEAL / DISCARD
- **STEAL:** the conversion math (`(x-127.4)/128.0`), the retry-20 pattern, the discrete-gain nearest-step logic,
  the `>1 GHz` re-tune PLL trick, the `{lna_agc, tuner_agc, bias, ppm, gain}` settings schema, and the
  "whitelist of valid samplerates" idea (**critical on a Pi**: 2.4 MSPS and 1.024 MSPS are the safe RTL modes;
  Elektro-L xRIT flows use 1 MSPS and 6 MSPS → the Airspy path is the realistic one for HRIT).
- **DISCARD:** the ImGui `drawControlUI`, the plugin/registry indirection, the dozens of SDR backends,
  and the `dsp::stream` ring-buffer machinery (we can use a simple SPSC ring buffer of our own).
- **DESIGN NOTE:** a single-process, compile-time-selected `SdrSource` (rtlsdr + airspy only) with
  `open/configure/start/stop` is enough. Bias-tee is mandatory for a 1691 MHz LNB.

---

## 3. GGAK pipeline (ELEKTRO-L GGAK-M / SEM TLM)

### 3.1 What it does
GGAK is the space-weather / radiation-monitor payload. Downlink is a **low-rate PM/BPSK telemetry at 1693 MHz**
(5 kbaud). We decode it, extract SKL/SKIF science records, and export them (GobDump pushes to MQTT).

### 3.2 Pipeline descriptor
[`Elektro_Arktika.json`](../GobDump_reference/resources/pipelines/Elektro_Arktika.json) → `elektro_ggak`:
```
samplerate: 3e6
frequencies: ["SEM TLM", 1693.0e6]
soft : module pm_demod   { symbolrate: 5e3, pll_bw: 0.02, rrc_alpha: 0.5 }
cadu : module ccsds_simple_psk_decoder { constellation bpsk, cadu_size 1792,
                                         derandomize false, nrzm false, rs_i 0 }
```
`elektro_ggak_mqtt` is identical plus a `ggak_to_mqtt` sink.

### 3.3 Key files / functions
- Plugin entry: [`elektro_arktika_support/main.cpp`](../GobDump_reference/plugins/elektro_arktika_support/main.cpp) (registers `GGAKInstrumentsDecoderModule`, `ELEKTROLRITDataDecoderModule`, `GGAKToMQTTModule`).
- Frame module: [`elektro_arktika/ggak/module_ggak_instruments.cpp`](../GobDump_reference/plugins/elektro_arktika_support/elektro_arktika/ggak/module_ggak_instruments.cpp).
- Parsing: [`ggak.cpp`](../GobDump_reference/plugins/elektro_arktika_support/elektro_arktika/ggak/ggak.cpp), [`skl.cpp`](../GobDump_reference/plugins/elektro_arktika_support/elektro_arktika/ggak/skl.cpp), [`skif.cpp`](../GobDump_reference/plugins/elektro_arktika_support/elektro_arktika/ggak/skif.cpp), [`plot.cpp`](../GobDump_reference/plugins/elektro_arktika_support/elektro_arktika/ggak/plot.cpp).
- Also `cli/ggak_ingestor.cpp`, `cli/ggak_merger.cpp` (offline post-processing subcommands).

### 3.4 CADU / framing / SNR
- **CADU/frame:** fixed **224-byte** records (`read_data(&frm,224)`). `GGAKFrame` has `id`; `id==48` is the SKL frame.
  Written verbatim to a `.cadu` file (`fsfsm_file_ext = ".cadu"`), enabled output.
- **Checksum:** [`checkCRC()`](../GobDump_reference/plugins/elektro_arktika_support/elektro_arktika/ggak/ggak.cpp:10) — **simple 16-bit sum** of bytes 0..221 compared to bytes 222/223 (big-endian). *No CCSDS CRC / no Reed-Solomon.*
- **Timing:** `getTimestamp()` = `frm->timestamp + 10957*86400 - 3*3600` (TDB→UNIX + MSK offset). ⚠️ note the hard `-3h` (ground station local time assumption) — must be re-derived for our station.
- **SKL parsing ([`skl.cpp`](../GobDump_reference/plugins/elektro_arktika_support/elektro_arktika/ggak/skl.cpp)):** 14 records of 14 bytes; 11 science channels. Non-linear channels (c==0 and c==9) use `exp((v-67)/29.02)*1000`; others linear `v*10`. `resampleSKLTo10s()` averages runs of 10.
- **SNR estimation (general):** *the GGAK module does not compute SNR itself* — SNR comes from the CCSDS/Viterbi
  layer (see §4.4) via [`EVMSNREstimator`](../GobDump_reference/src-core/common/dsp/utils/snr_estimator.h).

### 3.5 STEAL / DISCARD
- **STEAL:** the whole `elektro_ggak` parameter block (3 MSPS, 1693 MHz, PM 5 kbaud), the 224-byte framing + sum-CRC,
  the SKL 14×14 layout and its exp/linear de-quantisation constants, and the timestamp offset formula.
- **DISCARD:** MQTT sink, ImGui plot, the `cli/ggak_ingestor`/`merger` offline tools (we'll write our own CSV/JSON store).
- **RISK:** the sum-CRC is weak; and `-3h` is a **hard-coded assumption** — flag for validation.

---

## 4. LRIT / HRIT decode chain for Elektro-L (the heart of the project)

### 4.1 End-to-end path (IQ → full-disk image)

```mermaid
flowchart LR
    IQ[IQ samples] --> DEMOD[PSK/OQPSK demod]
    DEMOD --> SOFT[soft symbols]
    SOFT --> VIT[Viterbi 1/2 + depuncture]
    VIT --> DEF[BPSK_CCSDS_Deframer ASM 0x1ACFFC1D]
    DEF --> DER[derandomize PN 255]
    DER --> RS[Reed-Solomon rs223 interleave 4]
    RS --> CADU[8192-bit CADU]
    CADU --> VCDU[parseVCDU per VCID]
    VCDU --> MPDU[ccsds_aos Demuxer]
    MPDU --> PKT[CCSDS packets apid]
    PKT --> CRC[CRC-16 check]
    CRC --> SEG[XRITFile segment assembly]
    SEG --> SID[SegmentedImageDecoder]
    SID --> CAL[calibration + composites]
```

### 4.2 Pipeline descriptors
From [`Elektro_Arktika.json`](../GobDump_reference/resources/pipelines/Elektro_Arktika.json):

**`elektro_lrit`** (the schedule-driven full-disk capture):
```
samplerate: 1e6, freq 1691.0e6
soft : psk_demod  { constellation bpsk, symbolrate 294000, rrc_alpha 0.5, pll_bw 0.0145, max_sps 3 }
cadu : ccsds_conv_concat_decoder {
         constellation bpsk, cadu_size 8192,
         viterbi_ber_thresold 0.300, viterbi_outsync_after 20,
         derandomize true, rs_i 4, rs_type "rs223", rs_usecheck true }
products : elektro_lrit_data_decoder
```
**`elektro_hrit`** (higher rate):
```
samplerate: 6e6, freq 1691.0e6
soft : psk_demod  { constellation oqpsk, symbolrate 1.15e6, rrc_alpha 0.5, pll_bw 0.001, max_sps 3 }
cadu : ccsds_conv_concat_decoder { ...same as LRIT... }
products : elektro_lrit_data_decoder
```
**`elektro_rdas` / `arktika_rdas`** (raw MSU-GS instrument dumps, 30.72 Msps, 45 MSPS sample rate, BPSK,
`ccsds_simple_psk_decoder` cadu_size 8192, derandomize true, nrzm true) → `elektro_arktika_msugs`.

### 4.3 xRIT transport / demux (IQ→ segments)
[`plugins/xrit_support/xrit/transport/xrit_demux.cpp`](../GobDump_reference/plugins/xrit_support/xrit/transport/xrit_demux.cpp):
1. `XRITDemux::work(uint8_t* cadu)` (cadu = **1024 bytes**):
   - `parseVCDU(cadu)` — [`ccsds_aos/vcdu.cpp`](../GobDump_reference/src-core/common/ccsds/ccsds_aos/vcdu.cpp):
     `version=cadu[4]>>6`, `scid=((cadu[4]&0x3F)<<2)|(cadu[5]>>6)`, `vcid=cadu[5]&0x3F`,
     `counter=cadu[6..8]`, `replay=cadu[9]>>7`.
   - Skip `vcid==63` (filler). Otherwise lazily create a `ccsds::ccsds_aos::Demuxer(mpdu_size, false)` per VCID.
   - `Demuxer::work(cadu)` reassembles `CCSDSPacket`s (6-byte header, payload length from packet header;
     insert-zone configurable; handles headers split across CADUs). See
     [`ccsds_aos/demuxer.h`](../GobDump_reference/src-core/common/ccsds/ccsds_aos/demuxer.h).
   - Skip `apid==2047` (idle) and payloads < 2 bytes.
   - One `XRITFile` per **APID**; `sequence_flag` state machine:
     `1|3` = start (parse 10-byte in-payload header, reset), `0` = continuation, `2` = last (finalize).
   - **CRC-16/CCITT** ([`computeCRC`](../GobDump_reference/plugins/xrit_support/xrit/transport/xrit_demux.cpp:15), poly table in `crc_table.h`,
     init `0xffff`) over `payload[0..n-2]`, compared to last 2 bytes. On failure of a *middle* segment it can
     **recover** the image if the header says file_type_code==0 (image); otherwise the file is dropped.
2. Header structures: [`xrit/xrit_file.h`](../GobDump_reference/plugins/xrit_support/xrit/xrit_file.h) / [`xrit_file.cpp`](../GobDump_reference/plugins/xrit_support/xrit/xrit_file.cpp) (`PrimaryHeader`,
   `ImageStructureRecord`, `ImageNavigationRecord`, annotations, DCP…). Elektro LRIT segment id header:
   [`elektro_arktika/lrit/lrit_header.h`](../GobDump_reference/plugins/elektro_arktika_support/elektro_arktika/lrit/lrit_header.h)
   (`SegmentIdentificationHeader`, type=128: record_length, sc_id, channel_id, segment_sequence_number,
   planned_start/end_segment, compression).
3. Module glue: [`module_elektro_lrit_data_decoder.cpp`](../GobDump_reference/plugins/elektro_arktika_support/elektro_arktika/lrit/module_elektro_lrit_data_decoder.cpp)
   just loops: `read_data(cadu,1024)` → `XRITDemux::work` → `processLRITFile` (which delegates to `XRITChannelProcessor`).

### 4.4 Error-correction math (IQ→CADU)
[`src-core/pipeline/modules/ccsds/module_ccsds_conv_concat_decoder.cpp`](../GobDump_reference/src-core/pipeline/modules/ccsds/module_ccsds_conv_concat_decoder.cpp) (710 lines):
- **Soft symbols** read (int8) → optional `rotate_soft` (BPSK90 / IQ invert).
- **Viterbi pool** ([`codings/viterbi/`](../GobDump_reference/src-core/common/codings/viterbi)):
  `Viterbi1_2` (rate 1/2, k=7, `volk_k7_r2_generic_fixed.h`), and `Viterbi_Depunc` with `Depunc23/34/56/78`.
  `conv_rate:"auto"` runs **all 5 rates in parallel** and picks the lowest-BER locked one (with hysteresis:
  keep lock, switch only on drop). **BER** + `getState()` drive locks.
- **SNR:** [`EVMSNREstimator`](../GobDump_reference/src-core/common/dsp/utils/snr_estimator.h) — decision-directed
  error-vector-magnitude SNR, per-I/Q component, rotation-invariant; `snr()` returns dB. Also available:
  `M2M4SNREstimator` (2nd/4th moment, GNU Radio-derived).
- **Deframer:** [`codings/deframing/bpsk_ccsds_deframer.cpp`](../GobDump_reference/src-core/common/codings/deframing/bpsk_ccsds_deframer.cpp) —
  bit-level ASM search for `0x1ACFFC1D` (and its inverse), 3 states (`NOSYNC/SYNCING/SYNCED`), tolerant
  bit-error compare (`compare_32`), configurable `CADU_SIZE`/`CADU_PADDING`.
- **Derandomize:** [`codings/randomization.cpp`](../GobDump_reference/src-core/common/codings/randomization.cpp) — CCSDS PN:
  byte LUT `ccsds_pn[255]`, bit LUT `ccsds_soft_pn[255]`, and a fast **17-bit LFSR** variant `derand_ccsds17`
  (seed 0x38E3, poly `x^17+x^14+1`, 16384-byte LUT). `derand_ccsds()`, `derand_ccsds_soft()`.
- **Reed-Solomon:** [`codings/reedsolomon/`](../GobDump_reference/src-core/common/codings/reedsolomon) — `rs223`
  (223→255, i=4 interleave), `rs239`; `ReedSolomon(rstype, fill_bytes)`, dual-basis, `usecheck`.
- **Differential:** `nrzm` (NRZ-M) via [`codings/differential/nrzm.cpp`](../GobDump_reference/src-core/common/codings/differential/nrzm.cpp).
- Even simpler path (`ccsds_simple_psk_decoder`, used for RDAS/GGAK):
  [`module_ccsds_simple_psk_decoder.cpp`](../GobDump_reference/src-core/pipeline/modules/ccsds/module_ccsds_simple_psk_decoder.cpp) —
  no Viterbi, handles BPSK/QPSK/OQPSK soft-demod + NRZM + derand + deframe + RS.

### 4.5 IQ→image in the MSU-GS reader (RDAS path, not xRIT)
[`module_msugs.cpp`](../GobDump_reference/plugins/elektro_arktika_support/elektro_arktika/instruments/msugs/module_msugs.cpp):
- `SimpleDeframer(ASM=0x0218a7a392dd9abf, 64-bit, frame_len, …)` — **custom 64-bit ASM** (not CCSDS 0x1ACFFC1D).
- VCID routing: `vcid=(cadu[5]>>1)&7` → 2=VIS1, 5=VIS2, 3=VIS3, 4=IR; frames start at `cadu[24]`.
- Readers:
  - [`msu_vis_reader.cpp`](../GobDump_reference/plugins/elektro_arktika_support/elektro_arktika/instruments/msugs/msu_vis_reader.cpp): 10-bit packed → 6004-wide lines, deinterleave two channels, `<<6` to 16-bit; per-line timestamps; optional counter-correction ("lock" when `counter==global+1`, snap within ±7). 6004×17200 image.
  - [`msu_ir_reader.cpp`](../GobDump_reference/plugins/elektro_arktika_support/elektro_arktika/instruments/msugs/msu_ir_reader.cpp): 7 channels, 183 px each, 3400 lines; `channel*60` scale.
- Products → `ImageProduct` with `ChannelTransform` (affine / affine-interpx) for channel alignment.

### 4.6 STEAL / DISCARD
- **STEAL (highest value in the whole repo):**
  - The exact Elektro-L LRIT/HRIT/RDAS/GGAK parameter blocks (frequencies, symbol rates, Viterbi/RS settings).
  - `XRITDemux` algorithm: VCDU parse → per-VCID AOS demuxer → APID file assembly → CRC-16 recovery policy.
  - `BPSK_CCSDS_Deframer` state machine + dual ASM.
  - CCSDS PN derandomiser tables + 17-bit LFSR variant.
  - Viterbi 1/2 + depuncture pool with auto-rate + hysteresis.
  - `EVMSNREstimator` (we need SNR for the webui gauge).
  - RS rs223 interleave-4 + dual-basis + usecheck.
  - MSU-GS 64-bit ASM framing, 10-bit unpack, VCID map, counter-correction.
- **DISCARD:** the "pipeline descriptor JSON + EventBus register/unregister modules" plumbing,
  the `FileStreamToFileStreamModule` base (we'll stream directly), and the ImGui UI hooks.
- **RISK / SURPRISE:**
  - Elektro-L **LRIT is BPSK 294 kbaud @ 1691 MHz** and **HRIT is OQPSK 1.15 Mbaud** — both share the frequency;
    schedule must select constellation. Both use the *same* 8192-bit CADU + Viterbi + RS223(i=4).
  - **`X R I T` VCID filter drops vcid==63**; Elektro uses multiple VCIDs/APIDs — we must keep a per-APID file map.
  - The RDAS MSU-GS path is **custom framing** (64-bit ASM), not CCSDS — two distinct deframers needed.
  - GGAK uses **no Viterbi/RS** (already the "simple" path).
  - Deframing is **bit-level** and CPU-heavy; on a Pi 3B+ this is the main CPU budget along with Viterbi.

---

## 5. Image processing & composites

### 5.1 Channel handling
- Core image type: [`src-core/image/image.h`](../GobDump_reference/src-core/image/image.h) (+`image_utils`, `image_draw`, `processing`, `io/`),
  16-bit typical, 1/2/3/4 channels.
- Product channels: [`products/image_product.h`](../GobDump_reference/src-core/products/image_product.h) —
  `ImageHolder{abs_index, filename, channel_name, image, bit_depth, ch_transform, wavenumber, polarization, bandwidth, calibration_type}`.
  Channel lookup by index / name / **by specification (wavenumber ±tolerance, bandwidth ±tolerance, polarization)**.

### 5.2 RGB composites — the expression engine
[`products/image/product_expression.cpp`](../GobDump_reference/src-core/products/image/product_expression.cpp) (580 lines) +
[`product_expression.h`](../GobDump_reference/src-core/products/image/product_expression.h):
- Uses **muParser** ([`libs/muparser`](../GobDump_reference/src-core/libs/muparser)) to evaluate a small DSL.
- Primitives:
  - `cchN=(ch, unit, min, max)` — calibrated channel, e.g. `cch4=(4, brightness_temperature, 203, 323)`;
    output is normalised 0..1 after unit conversion + stretch.
  - `lut=lut(path.png); lut(expr, x, y)` — 2D LUT sampling (2D → 3 components).
  - `equp=equp(path.png)` — equirectangular-projected underlay (map) lookup.
  - Plain arithmetic (`1-cch4`, `^`, etc.). Output tuple of 1..N values → channels (+alpha possible).
- Composite presets live in [`resources/instrument_cfgs/*.json`](../GobDump_reference/resources/instrument_cfgs) as
  `{"name":..., "expression": "...", "description": "..."}`. Examples:
  `Day Cloud Convection`: `cch1^0.588,...,1-cch4`; `Cloud Top IR` via 1D LUT
  ([`common/cal_1d_lut/10um.json`](../GobDump_reference/resources/instrument_cfgs/common/cal_1d_lut/10um.json));
  `Split Cloud Phase` via 2D LUT ([`common/split_windows/geostationary.json`](../GobDump_reference/resources/instrument_cfgs/common/split_windows/geostationary.json)).
- **Scripted composites (Lua):** [`resources/scripted_compos/*.lua`](../GobDump_reference/resources/scripted_compos) —
  e.g. [`underlay_with_clouds_lut.lua`](../GobDump_reference/resources/scripted_compos/underlay_with_clouds_lut.lua)
  is the **IR cloud extraction over a map with transparency** template (see §5.4).

### 5.3 IR cloud extraction + transparency (the "cloud underlay")
Two mechanisms exist; **both are STEAL-worthy**:
1. **Lua underlay** ([`underlay_with_clouds_lut.lua`](../GobDump_reference/resources/scripted_compos/underlay_with_clouds_lut.lua)):
   - Loads a base map (`maps/nasa_hd.jpg`) into an equirectangular projection; equalises IR channels;
     for each output pixel inverse-projects to lat/lon, samples a 2D LUT (`lut/...png`) in the
     (ch0,ch1)-normalised space, blends LUT colour over the map by a `blend`/`thresold` alpha.
   - This is the algorithm for "clouds extracted from IR, composited over a physical map".
2. **Image-level alpha blend** ([`src-core/image/image_utils.cpp`](../GobDump_reference/src-core/image/image_utils.cpp)):
   - `blend_images()` — average/alpha-weighted merge of N RGBA layers; alpha = max, colour = Σ(c·a)/Σa.
   - `merge_images_opacity(img1,img2,op)` — proper **Porter-Duff "over"**:
     `ret_alpha = a2 + a1*(1-a2)`, `ret_c = (a2·c2 + a1·c1·(1-a2))/ret_alpha`.
   - [`image_draw.cpp`](../GobDump_reference/src-core/image/image_draw.cpp) `draw_image_alpha()` — per-pixel
     `c = fg·a + (1-a)·bg`, alpha kept as `max`. **This is the exact primitive for "IR with transparency".**
- **LUT resources:** [`resources/lut/*.png`](../GobDump_reference/resources/lut) (WXtoImg-style palettes for APT/MCIR),
  [`resources/lut/cal/`](../GobDump_reference/resources/lut) (CloudTopIR, MultiSpectral, CIMSSLUT for split windows).

### 5.4 Calibration
- Base: [`products/image/image_calibrator.h`](../GobDump_reference/src-core/products/image/image_calibrator.h),
  [`common/calibration.cpp`](../GobDump_reference/src-core/common/calibration.cpp), `calibration_units.cpp`.
- Units enum: reflective radiance, emissive radiance, brightness temperature, albedo, sun-angle-compensated
  reflective radiance, temperature↔radiance conversions ([`converters/`](../GobDump_reference/src-core/products/image/converters)).
- MSU-GS specifically:
  - [`xrit/elektro/msugs_xrit_calibrator.h`](../GobDump_reference/plugins/xrit_support/xrit/elektro/msugs_xrit_calibrator.h):
    `calibration_lut[channel][1024]`, invalid if `px==0 || px>1023`; channels ≥3 → `temperature_to_radiance`.
  - [`xrit/elektro/specific_proc.cpp`](../GobDump_reference/plugins/xrit_support/xrit/elektro/specific_proc.cpp):
    `tryApplyELEKTROCalib()` sets channels 0-2 = reflective radiance, ≥3 = emissive radiance, and sets
    wavenumber from `wavelengths[i]` (`freq_to_wavenumber(c/λ)`).
  - Tables: [`resources/calibration/MSUGS_table.json`](../GobDump_reference/resources/calibration/MSUGS_table.json),
    [`resources/elektro/l*_cfg.json`](../GobDump_reference/resources/elektro), [`resources/elektro/m*_cfg.json`](../GobDump_reference/resources/elektro).
- Channel transforms: [`products/image/channel_transform.h`](../GobDump_reference/src-core/products/image/channel_transform.h) —
  NONE/AFFINE/AFFINE_SLANTX/AFFINE_INTERPX/INTERP_XY; interpolation via thin-plate spline. Used to register
  MSU-GS channels (different detectors/scan axes) to a reference field.

### 5.5 STEAL / DISCARD
- **STEAL:** muParser expression DSL design (unit-aware `cch=(ch,unit,min,max)`, `lut()`, `equp()`),
  the `merge_images_opacity` Porter-Duff formula, `draw_image_alpha` blend, the MSU-GS calibration LUT
  approach + wavenumber assignment, 1D/2D LUT palette technique, and the cloud-underlay recipe.
- **DISCARD:** Lua scripting engine, ImGui product editor/handlers, CBOR product serialization (we'll use
  our own simple on-disk products), and the `ChannelTransform` JSON persistence layer.
- **SURPRISE:** the "IR cloud extraction with transparency" is **not a bespoke algorithm** — it is
  `draw_image_alpha` / `merge_images_opacity` + a 2D LUT + optional map underlay. We can reproduce it with
  ~50 lines and a palette PNG.

---

## 6. Projection

### 6.1 What it does
Self-contained projection engine — **no external PROJ dependency**. Supports forward (lat/lon→x/y) and
inverse (x/y→lat/lon), plus reprojection (resampling) of whole images.

### 6.2 Key files / functions
- Generic wrapper: [`projection/projection.h`](../GobDump_reference/src-core/projection/projection.h) — `class Projection`
  with `init(fwd,inv)`, `forward(pos,x,y)`, `inverse(x,y,pos,otime)`, `proj_type_t{STANDARD,RAYTRACER,THINPLATESPLINE}`.
- Standard projections: [`projection/standard/`](../GobDump_reference/src-core/projection/standard):
  - `proj.h/.cpp` — dispatch + `projection_t` (ellipsoid a,e,es,n; lam0,phi0,k0,x0,y0; offsets/scalars).
  - `equirect.cpp`, `geos.cpp` (geostationary/GEOS), `stereo.cpp`, `tmerc.cpp` (UTM), `tpers.cpp`, `webmerc.cpp`.
  - [`webmerc.cpp`](../GobDump_reference/src-core/projection/standard/webmerc.cpp): **fwd** `x=k0·λ`, `y=k0·asinh(tan φ)`;
    **inv** `φ=atan(sinh(y/k0))`, `λ=x/k0`. ✔ exactly what we need for Web Mercator WebP.
- Reprojection: [`projection/reprojector.h`](../GobDump_reference/src-core/projection/reprojector.h), `reproject_img.cpp`,
  `warp/warp.cpp`, `warp/smart_warp.cpp` (SAMPLING + optional OpenCL kernels in [`resources/opencl/`](../GobDump_reference/resources/opencl):
  `reproj_image_equ_to_*`, `reproj_image_merc_to_equ_fp32.cl`).
- Geostationary setup for Elektro comes from the xRIT navigation record:
  [`xrit_channel_processor_saveimg.cpp::getProjCfg()`](../GobDump_reference/plugins/xrit_support/xrit/processor/xrit_channel_processor_saveimg.cpp:55) —
  `type=geos`, `sweep_x=false` for MSUGS, `scalar_x=624597.0334223134`-derived, `altitude=35786023.0`,
  `lon0` from `image_navigation_record->projection_name` (`geos(76.0)` etc.).
- Thin-plate spline: [`projection/thinplatespline.cpp`](../GobDump_reference/src-core/projection/thinplatespline.cpp), `tps/latlontps_proj.*`.
- Equirectangular helper for underlays: [`projection/utils/equirectangular.cpp`](../GobDump_reference/src-core/projection/utils/equirectangular.cpp).

### 6.3 Axis-order gotcha
`ResourceReport`/`image/*` note projections differ in scan axis (`sweep_x`). Hydro: for **Elektro MSU-GS sweep_x=false**.

### 6.4 STEAL / DISCARD
- **STEAL:** the `projection_t` struct + per-type fwd/inv formulas (webmerc, geos, equirect), the
  geostationary `getProjCfg` derivation from the xRIT navigation record, and the "reproject by inverse
  mapping + bilinear sample" approach.
- **DISCARD:** OpenCL kernels (no GPU on Pi), the Raytracer/TPSSpline machinery (LOL-only), ImGui proj handler.
- **SURPRISE:** Web Mercator is trivially embeddable — we implement it directly. `proj_scalar_x` on Elektro
  comes out as ≈ **624597.0334223134 · k** (see `k` constant in the file) — we should compute it from the
  navigation record's column/line scaling factors.

---

## 7. Webserver / autotrack webui

### 7.1 What it does
A **small nng-based HTTP server** exposes JSON telemetry and a self-contained HTML status page; the GUI also
has an autotrack scheduler. For ElektroDump we only need the *idea*: HTTP + JSON status + a status page.

### 7.2 Key files / functions
- Server: [`src-cli/legacy/webserver.cpp`](../GobDump_reference/src-cli/legacy/webserver.cpp) — global
  `nng_http_server`, handlers registered with `nng_http_handler_alloc`:
  - `/api` → JSON (callback `handle_callback`),
  - `` (tree) → HTML (`handle_callback_html(uri)`),
  - `/polarplot.jpeg`, `/fft.jpeg`, `/schedule.jpeg` → JPEG bytes.
  All serialized by a single `request_mutex`.
- Autotrack UI: [`src-cli/legacy/autotrack/autotrack_web.cpp`](../GobDump_reference/src-cli/legacy/autotrack/autotrack_web.cpp) —
  builds inline HTML + JS (`XMLHttpRequest` polling `/status` every 1 s) with embedded CSS; JSON assembled
  from `live_pipeline->getModulesStats()`, `object_tracker.getStatus()`, VFO states, recording sizes.
  `web_heartbeat()` disables the FFT after 10 s of no access (lazy resource).
- Scheduler core: [`src-core/common/tracking/scheduler/scheduler.cpp`](../GobDump_reference/src-core/common/tracking/scheduler/scheduler.cpp),
  `scheduler_ui.cpp`, `passes.cpp` — `AutoTrackScheduler` with `AutoTrackCfg{multi_mode, stop_sdr_when_idle,
  autotrack_min_elevation, use_localtime}`, AOS/LOS/engage callbacks, pass prediction via TLE.
- Newer variant: [`src-core/core/cli/experimental/hserver.cpp`](../GobDump_reference/src-core/core/cli/experimental/hserver.cpp).

### 7.3 STEAL / DISCARD
- **STEAL:** the JSON status idea (`/api` returning per-module stats, FFT, object tracker, recording sizes);
  the self-contained HTML+JS+CSS page pattern (no framework, works on a phone); lazy-FTT style resource gating.
- **DISCARD:** `nng` as the HTTP server (heavy dep). For a Pi appliance a tiny embedded HTTP server
  (or even a handful of hand-rolled sockets + a static file server) is enough; we mostly need to serve
  **WebP images + JSON** over LAN. `nng` already ships MQTT/NNG for network streaming, but we can drop it.
- **SURPRISE:** autotrack is *pass-based (LEO)* — **irrelevant for Elektro-L (geostationary)**. Our "scheduler"
  is a **time-of-day capture schedule** (retune to LRIT/HRIT/GGAK windows), not a rotator/pass tracker.
  Keep the *task-scheduler* concept, discard the *sat pass tracker*.

---

## 8. Task queue & scheduler / threading model

### 8.1 Pieces
- **Offline pipeline:** [`pipeline/pipeline.h`](../GobDump_reference/src-core/pipeline/pipeline.h) — `Pipeline{id,name,steps[],preset,live_cfg}`,
  `steps` = `{level,module,parameters,input_override}`; `run()` chains modules by writing module N's output
  file/stream into module N+1. Modules are `ProcessingModule` with `DATA_FILE`/`DATA_STREAM` in/out.
- **Live pipeline:** [`pipeline/live_pipeline.h`](../GobDump_reference/src-core/pipeline/live_pipeline.h) — runs
  modules concurrently as `std::future<void>` on a **`ctpl::thread_pool`** ([`libs/ctpl/ctpl_stl.h`](../GobDump_reference/src-core/libs/ctpl/ctpl_stl.h)),
  connected by `dsp::stream<complex_t>`/byte streams. Also supports a **server/client split** over the network
  (packets of `pkt_size`, e.g. CADU size) for remote decoding.
- **Background jobs:** [`src-core/utils/task_queue.h`](../GobDump_reference/src-core/utils/task_queue.h)/`.cpp` —
  a **single-thread** "thread pool" (serial queue of `std::function<void()>`), cheap replacement for ctpl.
- **Timed tasks:** [`src-core/utils/task_scheduler.h`](../GobDump_reference/src-core/utils/task_scheduler.h)/`.cpp` —
  one thread + `condition_variable`; `add_task<T>(name, evt_ptr, last_run, run_interval)` / `del_task(name)`;
  run interval in seconds; tasks are **EventBus events** dispatched by `typeid(T).name()`. Used for periodic
  jobs (e.g. Kepler updates at 3-day intervals, see `satdump_general.kepler_update_interval`).
- **Thread priority:** [`src-core/utils/thread_priority.cpp`](../GobDump_reference/src-core/utils/thread_priority.cpp) —
  `setThreadPriority(th, prio)` and `setLowestThreadPriority()`; on Linux uses `SCHED_RR` / `SCHED_IDLE`;
  Windows uses `SetThreadPriority`. **Use this to deprioritise long image processing** so the SDR/DSP thread
  never starves on a Pi.
- **DSP blocks:** [`src-core/dsp/block.h`](../GobDump_reference/src-core/dsp/block.h), `base/dsp_buffer.*`,
  `base/readerwritercircularbuffer.h` (SPSC ring), `base/stream.h`. Each block runs in its own thread.
- **EventBus:** [`src-core/utils/event_bus.h`](../GobDump_reference/src-core/utils/event_bus.h) — typed pub/sub, core to plug-in registration.

### 8.2 STEAL / DISCARD
- **STEAL:** the *concepts* — (a) a small fixed thread pool with one thread per DSP stage; (b) a **time-based
  task scheduler** for "capture at HH:MM", "kepler refresh every N days", "cleanup old files"; (c)
  `setLowestThreadPriority` for image processing; (d) an SPSC ring buffer between IQ producer and demod.
- **DISCARD:** ctpl vendoring, the server/client VFO network model, the EventBus, and the
  `DATA_FILE/DATA_STREAM` module duality (we stream only).
- **DESIGN for Pi 3B+ (4×Cortex-A53):** budget one core for the SDR+demod chain, leave DSP-heavy
  Viterbi/deframe + image process on the remaining cores; use `SCHED_IDLE` for composite/projection so a
  scheduled LRIT capture never glitches.

---

## 9. Config system

### 9.1 What it does
- **Master defaults:** [`gobdump_cfg.json`](../GobDump_reference/gobdump_cfg.json) (493 lines, JSON-with-comments).
  Every setting is `{type, value, name, description}` where `type ∈ {bool, int, float, string, path,
  notated_int, options, labeled_options, password, baseband_type, ...}`.
  Sections: `user_interface`, `web_fft{enable,size,rate,avg,scale_min,scale_max}`, `satdump_general`
  (log_level, log_to_file, product_format, image_format, opencl_device, auto_process_products, QTH
  lat/lon/alt, kepler_update_interval, space-track creds), `satdump_directories`
  (recording_path, live_processing_path, default_input/output/image/projection dirs,
  `image_filename_template` with `$y $M $d $h $m $s $t $c $C $a $i $I $o $O` tokens),
  `kepler_settings` (urls, norads).
- **Loader:** [`src-core/core/config.cpp`](../GobDump_reference/src-core/core/config.cpp) `SatDumpConfigHandler`:
  `load(path, user_path)` → `default_cfg = loadJsonFile(path)`, then `loadUser()` merges a **diff** user
  `settings.json` via `merge_json_diffs(default, user)`; `saveUser()` writes only `perform_json_diff(default, main)`;
  `checkOutputDirs()` fixes `.`/Documents defaults. User file searched as `./settings.json` then `<user>/settings.json`.
- **Pipelines / presets as data:**
  - [`resources/pipelines/*.json`](../GobDump_reference/resources/pipelines) — the "pipelines" (module graphs) — loaded by `loadPipelines`.
  - [`resources/instrument_cfgs/*.json`](../GobDump_reference/resources/instrument_cfgs) — composite presets per instrument.
  - [`resources/pipeline_cfgs/*.json`](../GobDump_reference/resources/pipeline_cfgs) — per-pipeline instance settings (e.g. `MSUGS_Full_Disk.json` with `elektrol2_pos`/`elektrol3_pos`).
  - [`resources/elektro/l{2,3,5}_cfg.json`](../GobDump_reference/resources/elektro) — satellite geometry/uncertainty (meridian, radius, `uncertainty`).
- JSON: [`nlohmann/json.hpp`](../GobDump_reference/src-core/nlohmann/json.hpp) + helpers [`json_utils.cpp`](../GobDump_reference/src-core/nlohmann/json_utils.cpp)
  (`loadJsonFile`, `saveJsonFile`, `merge_json_diffs`, `perform_json_diff`, `getValueOrDefault`).

### 9.2 STEAL / DISCARD
- **STEAL:** the **typed-parameter JSON schema** `{type,value,name,description,options}` and the
  **master-default + user-diff** persistence model; the `image_filename_template` token set; the separation of
  *pipeline definition* vs *runtime settings*; the `elektro/lN_cfg.json` satellite geometry files.
- **DISCARD:** JSON-with-comments (non-standard, needs a permissive parser — `GBDT_JSON_ALLOW_COMMENTS`),
  the UI-section coupling, and OpenCL/QTH/kepler fields we don't need.
- **SURPRISE:** config is a *single flat blob* plus external resources; there is no schema validation.
  For ElektroDump we should keep the same simple approach (a default `config.json` + user override) but
  validate and version it.

---

## 10. Utilities

### 10.1 Inventory (all under [`src-core/utils/`](../GobDump_reference/src-core/utils) unless noted)
| File | Key API / purpose |
|---|---|
| [`file.h`](../GobDump_reference/src-core/utils/file.h)/`.cpp` | `getFilesize(path)`; `file/` iterators (`file_iterators.h`, `folder_file_iterators.h`, `zip_file_iterators.h`) |
| [`string.h`](../GobDump_reference/src-core/utils/string.h)/`.cpp` | `splitString`, `replaceAllStr`, `isStringPresent` (case-insensitive), `loadFileToString`, `ws2s`, `s2ws` |
| [`time.h`](../GobDump_reference/src-core/utils/time.h)/`.cpp` | `getTime()` (double, ms), `timestamp_to_string(ts, local)` → `YYYY/MM/DD HH:MM:SS` |
| [`http.h`](../GobDump_reference/src-core/utils/http.h)/`.cpp` | `ensure_curl_global_init()`, `perform_http_request`, `perform_http_request_post` (libcurl); `http_ratelimit.*` |
| [`stats.h`](../GobDump_reference/src-core/utils/stats.h)/`.cpp` | running stats, `get_median`, (used for timestamps) |
| [`unit_parser.h`](../GobDump_reference/src-core/utils/unit_parser.h)/`.cpp` | `parseUnitFromString(str,out,UNIT_*)`; `UNIT_METER`, `UNIT_HERTZ` tables |
| [`majority_law.h`](../GobDump_reference/src-core/utils/majority_law.h)/`.cpp` | bitwise majority vote across N arrays; `majority_law_vec` |
| [`thread_priority.h`](../GobDump_reference/src-core/utils/thread_priority.h)/`.cpp` | `setThreadPriority`, `setLowestThreadPriority` (see §8) |
| [`task_queue.h`](../GobDump_reference/src-core/utils/task_queue.h)/`.cpp` | serial background thread pool |
| [`task_scheduler.h`](../GobDump_reference/src-core/utils/task_scheduler.h)/`.cpp` | time-based scheduler (see §9) |
| [`binary.*`](../GobDump_reference/src-core/utils/binary.h) | bit helpers (`reverseBits` used in MSU-GS) |
| [`color.*`](../GobDump_reference/src-core/utils/color.h) | colour helpers |
| [`format.h`](../GobDump_reference/src-core/utils/format.h) | `svformat`, formatting |
| [`event_bus.h`](../GobDump_reference/src-core/utils/event_bus.h) | typed pub/sub |
| [`mqtt_client.*`](../GobDump_reference/src-core/utils/mqtt_client.h) | MQTT (GGAK export) |

### 10.2 STEAL / DISCARD
- **STEAL:** `getFilesize`, `splitString`/`replaceAllStr`, `timestamp_to_string`, `parseUnitFromString`
  (+ unit tables), `majority_law` (useful for repeated-frame voting), `reverseBits`, `get_median`,
  `getValueOrDefault`-style JSON access, and the `http` helpers (for TLE/space-track fetch).
- **DISCARD:** EventBus, MQTT client, the file-iterator hierarchy (over-abstracted), ImGui context helpers,
  `tasked_button.h`, `imgui_context_wrapper.h`.
- **SURPRISE:** `majority_law` is bit-level and header-only (template) — genuinely useful for Elektro LRIT
  where the same segment is repeated; and `get_median` is used to pick sensible product timestamps.

---

## 11. License & attribution obligations

- **GobDump_reference is GPLv3** ([`LICENSE`](../GobDump_reference/LICENSE), GNU GPL v3).
  It is derived from SatDump (GPLv3). Consequence: **any code copied from it forces ElektroDump to be GPLv3
  and to publish full corresponding source under GPL.**
- [`.gitmodules`](../GobDump_reference/.gitmodules) pulls `docs/res` from `git.satdump.org/SatDump/Resources.git`.
- Third-party license landscape ([`resources/credits.md`](../GobDump_reference/resources/credits.md)):
  - **DecompWT** (EUMETSAT) — used for MSG-xRIT wavelet decompression; **its own license** (check `DecompWT/LICENSE`).
  - **libtomcrypt** — public domain / WTFPL (used for GK2A DES).
  - Vendored bits (libaec, OpenJPEG, bzlib, miniz, muparser, Lua, AngelScript, imgui, nlohmann) each carry
    their own licenses.
- **Recommendation for ElektroDump:**
  1. **Clean-room reimplement** the *algorithms* documented here (algorithms, formulas, protocol facts,
     file-format layouts and constants are **not** copyrightable). Do **not** copy GobDump source verbatim
     or near-verbatim, and do **not** copy its file/struct organization.
  2. Keep an `ATTRIBUTION.md` crediting SatDump/GobDump as the *methodological* reference and listing the
     protocol documents (LRIT Mission Specific Document, CCSDS 131.0-B/132.0-B/133.0-B, EUMETSAT HRIT/LRIT).
  3. If we choose a permissive license for ElektroDump, we must ensure **zero** GPL-licensed code (including
     no copy-paste of CCSDS/deframer/PN tables). The CCSDS PN table and Viterbi trellis are standard
     published data — re-derive them from the spec to be safe.
  4. Any *copied* subset (e.g. DecompWT) would impose its own license — avoid unless required, and isolate
     it behind a documented boundary.

---

## 12. Cross-cutting surprises, risks & opportunities

**Risks**
1. **Two different framings coexist:** CCSDS 0x1ACFFC1D (LRIT/HRIT/GGAK xRIT) **and** the custom 64-bit
   `0x0218a7a392dd9abf` ASM (MSU-GS RDAS). Don't unify them.
2. **Elektro timestamps are hard-coded** (`-3h` in GGAK; `+1735204808.28 - 1888` in MSU-VIS). Must be
   re-derived/validated for our station and satellite.
3. **Viterbi + bit-level deframe are the CPU hot-spot** on a Pi 3B+; plan for `SCHED_IDLE` image work and
   consider fixed-rate Viterbi (not `auto`) once we know Elektro is rate 1/2.
4. **OpenCL/CUDA paths and `-march=native`** are traps for the Pi — disable and pin the CPU flags.
5. **CRC policies differ:** xRIT uses CRC-16/CCITT with an image-recovery fallback; GGAK uses a 16-bit sum;
   MSU-GS reads CRC fields but the readers ignore them. Each must be handled separately.
6. **`sweep_x` and axis order** differ per satellite; Elektro MSU-GS is `sweep_x=false`.
7. **RTL-SDR PLL >1 GHz** workaround and **bias-tee** are essential for 1691/1693 MHz.

**Opportunities**
1. The **expression compositor** (`cch/lut/equp`) plus `draw_image_alpha` gives us visual/IR/IR-cloud
   composites + transparency almost for free.
2. **Web Mercator is 4 lines of math** — WebP output is trivial.
3. The **typed JSON config + user diff** model is a clean, proven pattern to reuse.
4. `majority_law` + `blend_images` are ready-made for noise-robust cloud rendering.

**Explicit "STEAL FIRST" shortlist**
1. `resources/pipelines/Elektro_Arktika.json` parameter blocks (all demod + decoder constants).
2. `XRITDemux` algorithm (VCDU→AOS demux→APID files→CRC-16 recovery).
3. `BPSK_CCSDS_Deframer` + CCSDS PN + 17-bit LFSR derand.
4. Viterbi 1/2 + depuncture + `EVMSNREstimator`.
5. RS223 interleave-4.
6. MSU-GS 64-bit-ASM framing + 10-bit unpack + VCID map + counter-correction.
7. GGAK 224-byte frame + SKL 14×14 de-quantisation.
8. Expression compositor + Porter-Duff `merge_images_opacity` + `draw_image_alpha` + 2D LUT.
9. `webmerc` / `geos` projection formulas + geostationary proj-cfg derivation.
10. Typed JSON config + user-diff persistence; `timestamp_to_string`, `parseUnitFromString`, `get_median`.

**Explicit "DISCARD" shortlist**
- CMake + plugin `dlopen` + per-satellite plugin tree + EventBus + AngelScript + Lua.
- ImGui everywhere (GUI, module `drawUI`, source `drawControlUI`, product/projection handlers).
- OpenCL kernels, HDF5/UHD/SoapySDR/Bladerf/Lime/Pluto backends, ctpl, MQTT, nng, CBOR products.
- LEO pass-tracker autotrack (Electro-L is GEO — replace with a time-of-day capture scheduler).
