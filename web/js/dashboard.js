/* ElektroDump - dashboard widgets (M9). */
'use strict';

(function () {
  const snrHistory = [];
  let lastProductId = null;

  function setText(id, value) {
    const node = document.getElementById(id);
    if (node) { node.textContent = value; }
  }

  // Palette comes from the stylesheet so there is one source of colour truth.
  function cssVar(name, fallback) {
    const v = getComputedStyle(document.documentElement)
      .getPropertyValue(name).trim();
    return v || fallback;
  }

  function fitCanvas(canvas) {
    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth || canvas.width;
    const h = Number(canvas.getAttribute('height')) || 88;
    if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
      canvas.width = Math.round(w * dpr);
      canvas.height = Math.round(h * dpr);
    }
    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return { ctx: ctx, w: w, h: h };
  }

  function drawSpark() {
    const canvas = document.getElementById('spark');
    if (!canvas) { return; }
    const fit = fitCanvas(canvas);
    const ctx = fit.ctx;
    const W = fit.w;
    const H = fit.h;
    ctx.clearRect(0, 0, W, H);
    // hairline baseline, then the trace over it
    ctx.strokeStyle = cssVar('--rule-soft', '#262219');
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(0, H - 0.5);
    ctx.lineTo(W, H - 0.5);
    ctx.stroke();
    if (snrHistory.length < 2) { return; }
    const min = Math.min.apply(null, snrHistory);
    const max = Math.max.apply(null, snrHistory);
    const span = (max - min) || 1;
    ctx.strokeStyle = cssVar('--accent', '#5f9ea3');
    ctx.lineWidth = 1.25;
    ctx.beginPath();
    for (let i = 0; i < snrHistory.length; i++) {
      const x = i * W / (snrHistory.length - 1);
      const y = H - 4 - ((snrHistory[i] - min) / span) * (H - 10);
      if (i === 0) { ctx.moveTo(x, y); } else { ctx.lineTo(x, y); }
    }
    ctx.stroke();
  }

  function renderStatus(status) {
    const pipeline = status.pipeline || {};
    setText('dash-mode', status.mode || '-');
    setText('dash-satellite', status.satellite || '-');
    setText('dash-version', status.version || '-');
    setText('dash-state', pipeline.state || '-');
    const snrNode = document.getElementById('dash-snr');
    if (snrNode) {
      snrNode.innerHTML = ED.fmt(pipeline.snr_db, 2) + '<small>dB</small>';
    }
    setText('dash-ber', pipeline.locked ? ED.fmt(pipeline.ber, 6) : '-');
    const lockNode = document.getElementById('dash-lock');
    if (lockNode) {
      lockNode.innerHTML = ED.lockHtml(pipeline);
    }
    const caduNode = document.getElementById('dash-cadus');
    if (caduNode) {
      caduNode.textContent = String(pipeline.cadus_total || 0);
      caduNode.title = String(pipeline.cadus_bad || 0) + ' uncorrectable';
    }
    const sdr = status.sdr || {};
    if (sdr.available) {
      setText('dash-sdr', sdr.driver + ' @ ' +
        ED.fmt(sdr.center_freq_hz / 1e6, 3) + ' MHz, ' +
        ED.fmt(sdr.sample_rate_hz / 1e6, 2) + ' MSPS');
    } else {
      setText('dash-sdr', 'unavailable');
    }
    const schedule = status.schedule || {};
    setText('dash-next', schedule.next_entry
      ? (schedule.next_entry + ' in ' + ED.fmtDuration(schedule.seconds_to_next))
      : 'none');

    const snr = Number(pipeline.snr_db);
    if (!isNaN(snr)) {
      snrHistory.push(snr);
      if (snrHistory.length > 120) { snrHistory.shift(); }
      drawSpark();
    }
  }

  function renderLatest(status) {
    const meta = (status.latest_products || {}).visual;
    const img = document.getElementById('latest-image');
    const caption = document.getElementById('latest-caption');
    if (!img || !caption) { return; }
    const empty = document.getElementById('latest-empty');
    if (!meta) {
      img.style.display = 'none';
      if (empty) { empty.style.display = ''; }
      caption.textContent = '';
      return;
    }
    img.style.display = '';
    if (empty) { empty.style.display = 'none'; }
    if (meta.id !== lastProductId) {
      lastProductId = meta.id;
      img.src = '/api/v1/products/' + encodeURIComponent(meta.id) + '/image';
    }
    caption.textContent = meta.id + ' · ' + meta.width + '×' + meta.height +
      ' · ' + meta.created;
  }

  async function tick() {
    try {
      const status = await ED.getJson('/api/v1/status');
      renderStatus(status);
      renderLatest(status);
    } catch (error) {
      ED.toast(error.message);
    }
  }

  ED.setPage('index');
  ED.startStatusPolling();
  ED.repeatWhileVisible(tick, 1000);
})();
