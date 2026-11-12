/* ElektroDump - live FFT spectrum + scrolling waterfall (M9). */
'use strict';

(function () {
  function cssVar(name, fallback) {
    const v = getComputedStyle(document.documentElement)
      .getPropertyValue(name).trim();
    return v || fallback;
  }
  function accentColour() { return cssVar('--accent', '#5f9ea3'); }
  function mapBg() { return cssVar('--bg', '#14120e'); }
  let ready = false;
  let smooth = null;                 // EMA of the spectrum, for the trace
  let lo = null, hi = null;          // smoothed display range, in dB

  function colour(t) {
    // Sequential ramp, not a rainbow: the ground darkens into the same petrol
    // as the spectrum trace, then lifts to bone at the top end. One hue carries
    // strength, so a weak signal and a strong one differ in brightness, not in
    // an arbitrary hue. t in 0..1.
    // Paper ground to ink: on a light surface a stronger signal has to read as
    // darker, the way a chart recorder or a printed spectrum does. Same single
    // hue as the spectrum trace, so the two panels agree.
    // Starts at a light grey rather than pure white: on paper the noise floor
    // has to be visible as a band, otherwise the panel looks empty until a
    // carrier appears.
    const stops = [
      [232, 236, 237], [198, 216, 216], [138, 180, 182],
      [44, 104, 109], [16, 44, 47]
    ];
    const scaled = Math.min(0.9999, Math.max(0, t)) * (stops.length - 1);
    const index = Math.floor(scaled);
    const frac = scaled - index;
    const a = stops[index];
    const b = stops[Math.min(stops.length - 1, index + 1)];
    return [
      Math.round(a[0] + (b[0] - a[0]) * frac),
      Math.round(a[1] + (b[1] - a[1]) * frac),
      Math.round(a[2] + (b[2] - a[2]) * frac)
    ];
  }

  function percentile(sorted, p) {
    const i = Math.min(sorted.length - 1,
      Math.max(0, Math.floor(p * (sorted.length - 1))));
    return sorted[i];
  }

  // Scale to what is actually on screen. The absolute bin level depends on
  // gain, LNA and band, so the old fixed -120..-20 dBFS window left a real
  // signal a single flat colour. Floor at the 10th percentile, top at the
  // 99.5th, with a minimum span so a dead band is not stretched into pure noise,
  // and the range is smoothed across frames so the picture does not pump.
  function updateRange(bins) {
    const sorted = Array.from(bins).sort(function (a, b) { return a - b; });
    const noise = percentile(sorted, 0.50);
    const nextHi = percentile(sorted, 0.995);
    // Place the noise floor just above the bottom edge: with lo = noise - m*span
    // the floor sits at m/(m+1) of the height, so m = 0.11 puts it at ~10% and
    // leaves the rest of the plot for peaks.
    const topSpan = Math.max(nextHi - noise, 6);
    let nextLo = noise - 0.11 * topSpan;
    if (nextHi - nextLo < 12) { nextLo = nextHi - 12; }
    if (lo === null || hi === null) {
      lo = nextLo;
      hi = nextHi;
    } else {
      lo += 0.25 * (nextLo - lo);
      hi += 0.25 * (nextHi - hi);
    }
  }

  function scale(value) {
    const span = (hi - lo) || 1;
    return Math.min(1, Math.max(0, (value - lo) / span));
  }

  function drawSpectrum(bins) {
    const canvas = document.getElementById('spectrum');
    const ctx = canvas.getContext('2d');
    canvas.width = 1024;
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    if (!bins || bins.length === 0) { return; }

    // Temporal smoothing of the trace: even 8 averaged FFT blocks are visibly
    // jittery frame to frame.
    if (!smooth || smooth.length !== bins.length) {
      smooth = Float32Array.from(bins);
    } else {
      for (let i = 0; i < bins.length; i++) {
        smooth[i] = smooth[i] * 0.6 + bins[i] * 0.4;
      }
    }

    ctx.strokeStyle = accentColour();
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let i = 0; i < smooth.length; i++) {
      const x = i * canvas.width / (smooth.length - 1);
      const y = canvas.height - 2 - scale(smooth[i]) * (canvas.height - 6);
      if (i === 0) { ctx.moveTo(x, y); } else { ctx.lineTo(x, y); }
    }
    ctx.stroke();
  }

  function pushWaterfall(bins) {
    const canvas = document.getElementById('waterfall');
    const ctx = canvas.getContext('2d');
    if (!ready || canvas.width !== bins.length) {
      canvas.width = bins.length;
      canvas.height = 200;
      ctx.fillStyle = mapBg();
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      ready = true;
    } else {
      ctx.drawImage(canvas, 0, 1);
    }
    const row = ctx.createImageData(bins.length, 1);
    for (let i = 0; i < bins.length; i++) {
      const rgb = colour(scale(bins[i]));
      row.data[i * 4] = rgb[0];
      row.data[i * 4 + 1] = rgb[1];
      row.data[i * 4 + 2] = rgb[2];
      row.data[i * 4 + 3] = 255;
    }
    ctx.putImageData(row, 0, 0);
  }

  async function tick() {
    const status = document.getElementById('wf-status');
    try {
      const frame = await ED.getJson('/api/v1/fft');
      const bins = frame.bins || [];
      if (bins.length === 0) { throw new Error('empty'); }
      updateRange(bins);
      drawSpectrum(bins);
      pushWaterfall(bins);
      status.textContent =
        bins.length + ' bins · ' + ED.fmt(frame.center_hz / 1e6, 3) + ' MHz · ' +
        ED.fmt(frame.span_hz / 1e6, 2) + ' MHz span · ' +
        ED.fmt(lo, 1) + '…' + ED.fmt(hi, 1) + ' dB';
    } catch (error) {
      status.textContent = 'spectrum unavailable (SDR not running)';
    }
  }

  ED.setPage('waterfall');
  ED.startStatusPolling();
  ED.repeatWhileVisible(tick, 500);
})();
