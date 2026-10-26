/* ElektroDump — live FFT spectrum + scrolling waterfall (M9). */
'use strict';

(function () {
  let lastFft = 0;
  let ready = false;

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

  function drawSpectrum(bins) {
    const canvas = document.getElementById('spectrum');
    const ctx = canvas.getContext('2d');
    canvas.width = 1024;
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    if (!bins || bins.length === 0) { return; }
    let min = Infinity, max = -Infinity;
    for (const value of bins) {
      if (value < min) { min = value; }
      if (value > max) { max = value; }
    }
    const span = (max - min) || 1;
    ctx.strokeStyle = '#4fd1c5';
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let i = 0; i < bins.length; i++) {
      const x = i * canvas.width / bins.length;
      const y = canvas.height - 2 - ((bins[i] - min) / span) *
        (canvas.height - 6);
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
      // Map dBFS (-120..-20) to 0..1.
      const t = (bins[i] + 120) / 100;
      const rgb = colour(t);
      row.data[i * 4] = rgb[0];
      row.data[i * 4 + 1] = rgb[1];
      row.data[i * 4 + 2] = rgb[2];
      row.data[i * 4 + 3] = 255;
    }
    ctx.putImageData(row, 0, 0);
  }

  async function tick() {
    try {
      const frame = await ED.getJson('/api/v1/fft');
      const bins = frame.bins || [];
      drawSpectrum(bins);
      pushWaterfall(bins);
      document.getElementById('wf-status').textContent =
        bins.length + ' bins · ' + ED.fmt(frame.center_hz / 1e6, 3) + ' MHz · ' +
        ED.fmt(frame.span_hz / 1e6, 2) + ' MHz span';
    } catch (error) {
      document.getElementById('wf-status').textContent =
        'spectrum unavailable (SDR not running)';
    }
  }

  ED.setPage('waterfall');
  ED.startStatusPolling();
  ED.repeatWhileVisible(tick, 500);
})();
