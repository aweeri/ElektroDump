/* ElektroDump - one instrument: spectrum sitting on top of the waterfall,
   shared frequency axis, dB scale down the left, pointer readout. */
'use strict';

(function () {
  const plot = document.getElementById('scope-plot');
  const specCanvas = document.getElementById('spectrum');
  const fallCanvas = document.getElementById('waterfall');
  const overlay = document.createElement('canvas');
  overlay.id = 'scope-overlay';
  plot.insertBefore(overlay, document.getElementById('scope-x'));

  const specCtx = specCanvas.getContext('2d');
  const fallCtx = fallCanvas.getContext('2d');
  const overCtx = overlay.getContext('2d');

  const SPEC_H = 132;
  const FALL_H = 300;

  let frame = null;          // { bins, center_hz, span_hz }
  let smooth = null;         // EMA of the spectrum
  let lo = null, hi = null;  // smoothed dB window
  let width = 0;             // device pixels
  let cssWidth = 0;
  let hoverX = null;         // CSS px inside the plot
  let primed = false;

  function cssVar(name, fallback) {
    const v = getComputedStyle(document.documentElement)
      .getPropertyValue(name).trim();
    return v || fallback;
  }

  // Paper ground to ink: strength reads as darkness, the way a printed spectrum
  // does. Light floor so an idle band is still visible as a band.
  function colour(t) {
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

  // Autoscale to what is on screen: floor at the median, top at the 99.5th
  // percentile, at least 12 dB of span, smoothed so the picture does not pump.
  function updateRange(bins) {
    const sorted = Array.from(bins).sort(function (a, b) { return a - b; });
    const noise = percentile(sorted, 0.50);
    const nextHi = percentile(sorted, 0.995);
    const topSpan = Math.max(nextHi - noise, 6);
    // 0.2 of the span below the median: at 0.11 the noise floor sat exactly on
    // the bottom edge and every quiet bin was clamped into one flat line, which
    // reads as a drawing error rather than an empty band.
    let nextLo = noise - 0.20 * topSpan;
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

  function fit() {
    const dpr = window.devicePixelRatio || 1;
    cssWidth = plot.clientWidth || 800;
    width = Math.round(cssWidth * dpr);
    for (const [canvas, h] of [[specCanvas, SPEC_H], [fallCanvas, FALL_H],
                               [overlay, SPEC_H + FALL_H]]) {
      if (canvas.width !== width || canvas.height !== Math.round(h * dpr)) {
        canvas.width = width;
        canvas.height = Math.round(h * dpr);
      }
      canvas.style.width = '100%';
      canvas.style.height = h + 'px';
      // Text and hairlines want CSS coordinates. The waterfall is pure pixels:
      // it scrolls by one device row per frame, so it must stay untransformed.
      if (canvas === fallCanvas) {
        canvas.getContext('2d').setTransform(1, 0, 0, 1, 0, 0);
      } else {
        canvas.getContext('2d').setTransform(dpr, 0, 0, dpr, 0, 0);
      }
    }
    overlay.style.width = '100%';
    overlay.style.height = (SPEC_H + FALL_H) + 'px';
    if (!primed) { primeWaterfall(); primed = true; }
  }

  function drawAxes() {
    const y = document.getElementById('scope-y');
    const x = document.getElementById('scope-x');
    const ticks = 4;
    let html = '';
    for (let i = 0; i <= ticks; i++) {
      const value = hi - (hi - lo) * (i / ticks);
      // Sit on the graticule drawn in drawSpectrum, not on an even split of the
      // canvas height, or the numbers float away from their lines.
      const top = (SPEC_H - 6) * (i / ticks);
      html += '<span style="top:' + top.toFixed(1) + 'px">' +
        value.toFixed(0) + '</span>';
    }
    y.innerHTML = html;

    if (frame) {
      const spanMhz = frame.span_hz / 1e6;
      const steps = 6;
      let labels = '';
      for (let i = 0; i <= steps; i++) {
        const offset = (-0.5 + i / steps) * spanMhz;
        labels += '<span>' + (Math.abs(offset) < 1e-6 ? '0' :
          (offset > 0 ? '+' : '') + offset.toFixed(2)) + '</span>';
      }
      x.innerHTML = labels;
      document.getElementById('wf-title').textContent =
        'Spectrum and waterfall - ' + ED.fmt(frame.center_hz / 1e6, 3) +
        ' MHz, ' + ED.fmt(spanMhz, 2) + ' MHz span, MHz from centre';
    }
  }

  function drawSpectrum() {
    specCtx.clearRect(0, 0, cssWidth, SPEC_H);
    if (!smooth) { return; }

    // dB graticule, the same values as the labels on the left
    const rule = cssVar('--rule-soft', '#ecedee');
    const muted = cssVar('--muted', '#6b7280');
    specCtx.save();
    specCtx.strokeStyle = rule;
    specCtx.lineWidth = 1;
    specCtx.font = '9px "Plex Mono", monospace';
    specCtx.fillStyle = muted;
    for (let i = 0; i <= 4; i++) {
      const y = Math.round((SPEC_H - 6) * (i / 4)) + 0.5;
      specCtx.beginPath();
      specCtx.moveTo(0, y);
      specCtx.lineTo(cssWidth, y);
      specCtx.stroke();
    }
    specCtx.restore();

    const n = smooth.length;
    const xOf = function (i) { return i * cssWidth / (n - 1); };
    const yOf = function (v) { return SPEC_H - 6 - scale(v) * (SPEC_H - 12); };

    // wash under the trace, so the shape reads as a solid silhouette
    const accent = cssVar('--accent', '#1c6469');
    specCtx.beginPath();
    specCtx.moveTo(0, SPEC_H - 6);
    for (let i = 0; i < n; i++) {
      specCtx.lineTo(xOf(i), yOf(smooth[i]));
    }
    specCtx.lineTo(cssWidth, SPEC_H - 6);
    specCtx.closePath();
    specCtx.fillStyle = accent;
    specCtx.globalAlpha = 0.10;
    specCtx.fill();
    specCtx.globalAlpha = 1;

    specCtx.strokeStyle = accent;
    specCtx.lineWidth = 1.1;
    specCtx.beginPath();
    for (let i = 0; i < n; i++) {
      if (i === 0) { specCtx.moveTo(xOf(i), yOf(smooth[i])); }
      else { specCtx.lineTo(xOf(i), yOf(smooth[i])); }
    }
    specCtx.stroke();
  }

  // One FFT frame every 500 ms: at a single device row per frame the record
  // crawls and the panel looks empty. A 3 CSS px band per frame gives a
  // readable scroll (~3 min of history across the height on a 2x display).
  function rowStep() {
    return Math.max(1, Math.round(3 * (window.devicePixelRatio || 1)));
  }

  function pushRow() {
    if (!smooth) { return; }
    const step = rowStep();
    fallCtx.drawImage(fallCanvas, 0, step);
    const band = fallCtx.createImageData(width, step);
    const n = smooth.length;
    for (let px = 0; px < width; px++) {
      const bin = Math.min(n - 1, Math.floor(px * n / width));
      // A 3-bin mean before colouring: per-bin noise otherwise paints the
      // record with vertical streaks that look like a rendering fault.
      const a = smooth[Math.max(0, bin - 1)];
      const b = smooth[bin];
      const c = smooth[Math.min(n - 1, bin + 1)];
      const rgb = colour(scale((a + b + c) / 3));
      for (let y = 0; y < step; y++) {
        const at = (y * width + px) * 4;
        band.data[at] = rgb[0];
        band.data[at + 1] = rgb[1];
        band.data[at + 2] = rgb[2];
        band.data[at + 3] = 255;
      }
    }
    fallCtx.putImageData(band, 0, 0);
  }

  // Prime the record with the floor colour so the panel reads as a running
  // chart recorder from the first second instead of a blank rectangle.
  function primeWaterfall() {
    const ground = colour(0);
    fallCtx.fillStyle = 'rgb(' + ground[0] + ',' + ground[1] + ',' +
      ground[2] + ')';
    fallCtx.fillRect(0, 0, fallCanvas.width, fallCanvas.height);
  }

  function drawOverlay() {
    overCtx.clearRect(0, 0, cssWidth, SPEC_H + FALL_H);
    if (!frame || hoverX === null || !smooth) { return; }
    const n = smooth.length;
    const bin = Math.min(n - 1,
      Math.max(0, Math.round(hoverX / Math.max(1, cssWidth) * (n - 1))));

    const now = cssVar('--error', '#b0392c');
    overCtx.strokeStyle = now;
    overCtx.globalAlpha = 0.55;
    overCtx.lineWidth = 1;
    overCtx.beginPath();
    overCtx.moveTo(Math.round(hoverX) + 0.5, 0);
    overCtx.lineTo(Math.round(hoverX) + 0.5, SPEC_H + FALL_H);
    overCtx.stroke();

    // dot on the trace at the hovered bin
    const y = SPEC_H - 6 - scale(smooth[bin]) * (SPEC_H - 12);
    overCtx.globalAlpha = 1;
    overCtx.fillStyle = now;
    overCtx.beginPath();
    overCtx.arc(hoverX, y, 2.5, 0, Math.PI * 2);
    overCtx.fill();

    const offset = (bin / (n - 1) - 0.5) * frame.span_hz;
    const mhz = (frame.center_hz + offset) / 1e6;
    document.getElementById('scope-read').textContent =
      ED.fmt(mhz, 4) + ' MHz  (' + (offset >= 0 ? '+' : '') +
      ED.fmt(offset / 1e3, 1) + ' kHz)  ' + ED.fmt(smooth[bin], 1) + ' dB';
  }

  plot.addEventListener('pointermove', function (event) {
    const rect = plot.getBoundingClientRect();
    hoverX = Math.min(rect.width, Math.max(0, event.clientX - rect.left));
    drawOverlay();
  });
  plot.addEventListener('pointerleave', function () {
    hoverX = null;
    document.getElementById('scope-read').textContent =
      'point at the plot to read level and frequency';
    drawOverlay();
  });

  async function tick() {
    try {
      const data = await ED.getJson('/api/v1/fft');
      const bins = data.bins || [];
      if (!bins.length) { throw new Error('empty'); }
      frame = data;
      if (!smooth || smooth.length !== bins.length) {
        smooth = Float32Array.from(bins);
      } else {
        for (let i = 0; i < bins.length; i++) {
          smooth[i] = smooth[i] * 0.6 + bins[i] * 0.4;
        }
      }
      updateRange(bins);
      fit();
      drawAxes();
      drawSpectrum();
      pushRow();
      drawOverlay();
    } catch (error) {
      document.getElementById('scope-read').textContent =
        'spectrum unavailable (SDR not running)';
    }
  }

  window.addEventListener('resize', function () { fit(); drawOverlay(); });

  ED.setPage('waterfall');
  ED.startStatusPolling();
  ED.repeatWhileVisible(tick, 500);
})();
