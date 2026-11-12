/* ElektroDump - constellation scatter (symbol cloud). */
'use strict';

(function () {
  const canvas = document.getElementById('constellation');
  const ctx = canvas.getContext('2d');
  function cssVar(name, fallback) {
    const v = getComputedStyle(document.documentElement)
      .getPropertyValue(name).trim();
    return v || fallback;
  }

  // Render at device resolution; the attribute size is a fallback only.
  function fit() {
    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth || 520;
    const h = w;
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return { w: w, h: h };
  }

  function setText(id, value) {
    const node = document.getElementById(id);
    if (node) { node.textContent = value; }
  }

  function clear(w, h) {
    ctx.clearRect(0, 0, w, h);
    // Graticule in the same hairline ink as every rule in the UI, so the plot
    // reads as instrument glass rather than a chart with its own palette.
    ctx.strokeStyle = 'rgba(20,23,26,0.38)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(Math.round(w / 2) + 0.5, 0); ctx.lineTo(Math.round(w / 2) + 0.5, h);
    ctx.moveTo(0, Math.round(h / 2) + 0.5); ctx.lineTo(w, Math.round(h / 2) + 0.5);
    ctx.stroke();
    ctx.strokeStyle = 'rgba(20,23,26,0.16)';
    for (const r of [0.5, 1.0]) {
      ctx.beginPath();
      ctx.arc(w / 2, h / 2, r * (w / 2 - 10) / 1.5, 0, Math.PI * 2);
      ctx.stroke();
    }
  }

  function draw(points) {
    const fitv = fit();
    const w = fitv.w, h = fitv.h;
    const span = 1.5;                       // plot +/- 1.5 RMS
    const k = (w / 2 - 10) / span;
    clear(w, h);
    ctx.fillStyle = cssVar('--accent', '#5f9ea3');
    ctx.globalAlpha = 0.62;
    for (const p of points) {
      const x = w / 2 + p[0] * k;
      const y = h / 2 - p[1] * k;
      if (x < 0 || x > w || y < 0 || y > h) { continue; }
      ctx.fillRect(x - 0.75, y - 0.75, 1.5, 1.5);
    }
    ctx.globalAlpha = 1;
  }

  async function tick() {
    try {
      const data = await ED.getJson('/api/v1/constellation?limit=3000');
      draw(data.points || []);
      setText('const-count', String(data.count || 0));
      setText('const-rms', Number(data.rms || 0).toExponential(2));
      setText('const-mode', ED.modeLabel(data.mode) || '-');
      setText('const-legend', (data.count || 0) + ' symbols');
      const status = await ED.getJson('/api/v1/status');
      const pipeline = status.pipeline || {};
      setText('const-snr', ED.fmt(pipeline.snr_db, 2) + ' dB');
      setText('const-ber', pipeline.locked ? ED.fmt(pipeline.ber, 6) : '-');
      const lockNode = document.getElementById('const-lock');
      if (lockNode) {
        lockNode.innerHTML = ED.lockHtml(pipeline);
      }
    } catch (error) {
      // 503 before the first symbols exist, or while the mode switches.
      setText('const-legend', 'no symbols yet (' + error.message + ')');
    }
  }

  ED.setPage('constellation');
  ED.startStatusPolling();
  ED.repeatWhileVisible(tick, 1000);
})();
