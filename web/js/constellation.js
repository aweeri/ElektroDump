/* ElektroDump — constellation scatter (symbol cloud). */
'use strict';

(function () {
  const canvas = document.getElementById('constellation');
  const ctx = canvas.getContext('2d');
  const accent = getComputedStyle(document.documentElement)
    .getPropertyValue('--accent') || '#4fd1c5';

  function setText(id, value) {
    const node = document.getElementById(id);
    if (node) { node.textContent = value; }
  }

  function clear() {
    ctx.fillStyle = 'rgba(0,0,0,0)';
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    const w = canvas.width, h = canvas.height;
    ctx.strokeStyle = 'rgba(127,127,127,0.25)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(w / 2, 0); ctx.lineTo(w / 2, h);
    ctx.moveTo(0, h / 2); ctx.lineTo(w, h / 2);
    ctx.stroke();
    ctx.strokeStyle = 'rgba(127,127,127,0.12)';
    for (const r of [0.5, 1.0]) {
      ctx.beginPath();
      ctx.arc(w / 2, h / 2, r * (w / 2 - 6) / 1.5, 0, Math.PI * 2);
      ctx.stroke();
    }
  }

  function draw(points) {
    const w = canvas.width, h = canvas.height;
    const span = 1.5;                       // plot +/- 1.5 RMS
    const k = (w / 2 - 6) / span;
    clear();
    ctx.fillStyle = accent.startsWith('#') ? accent : '#4fd1c5';
    ctx.globalAlpha = 0.55;
    for (const p of points) {
      const x = w / 2 + p[0] * k;
      const y = h / 2 - p[1] * k;
      if (x < 0 || x > w || y < 0 || y > h) { continue; }
      ctx.fillRect(x - 1, y - 1, 2, 2);
    }
    ctx.globalAlpha = 1;
  }

  async function tick() {
    try {
      const data = await ED.getJson('/api/v1/constellation?limit=3000');
      draw(data.points || []);
      setText('const-count', String(data.count || 0));
      setText('const-rms', Number(data.rms || 0).toExponential(2));
      setText('const-mode', data.mode || '—');
      setText('const-legend', (data.count || 0) + ' symbols');
      const status = await ED.getJson('/api/v1/status');
      const pipeline = status.pipeline || {};
      setText('const-snr', ED.fmt(pipeline.snr_db, 2) + ' dB');
      setText('const-ber', pipeline.locked ? ED.fmt(pipeline.ber, 6) : '—');
      setText('const-lock', pipeline.locked ? 'locked' : 'no lock');
    } catch (error) {
      // 503 before the first symbols exist, or while the mode switches.
      setText('const-legend', 'no symbols yet (' + error.message + ')');
    }
  }

  ED.setPage('constellation');
  ED.startStatusPolling();
  ED.repeatWhileVisible(tick, 1000);
})();
