/* ElektroDump — live FFT spectrum + scrolling waterfall (M9). */
'use strict';

(function () {
  let ready = false;
  let smooth = null;                 // EMA of the spectrum, for the trace
  let lo = null, hi = null;          // smoothed display range, in dB

  function colour(t) {
    // Blue -> cyan -> yellow -> red ramp, t in 0..1.
    const stops = [
      [10, 20, 60], [40, 120, 200], [80, 220, 220],
      [230, 220, 90], [230, 70, 60]
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
    let nextLo = percentile(sorted, 0.10);
    let nextHi = percentile(sorted, 0.995);
    const minSpan = 24;
    if (nextHi - nextLo < minSpan) { nextHi = nextLo + minSpan; }
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

    ctx.strokeStyle = '#4fd1c5';
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
      ctx.fillStyle = '#060a0f';
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
