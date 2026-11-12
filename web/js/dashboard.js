/* ElektroDump - dashboard widgets (M9). */
'use strict';

(function () {
  const snrHistory = [];
  const HISTORY_LIMIT = 300;
  let lastProductId = null;
  let seeded = false;

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
    const w = canvas.clientWidth || 900;
    // Layout height comes from CSS, never from the attribute: the attribute is
    // the device-pixel backing store and is rewritten below, so reading it back
    // multiplies the height by the DPR on every frame.
    const h = canvas.clientHeight || 88;
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
    let min = Math.min.apply(null, snrHistory);
    let max = Math.max.apply(null, snrHistory);
    // At least a 10 dB window, so a quiet stretch is not magnified into drama.
    if (max - min < 10) {
      const mid = (max + min) / 2;
      min = mid - 5;
      max = mid + 5;
    }
    const span = (max - min) || 1;
    // Two series: the raw one-second samples faint, and the five-sample mean
    // over them in the accent. 1 Hz SNR is spiky enough that a single trace
    // reads as noise rather than as a level.
    const smooth = [];
    for (let i = 0; i < snrHistory.length; i++) {
      const from = Math.max(0, i - 4);
      let sum = 0;
      for (let j = from; j <= i; j++) { sum += snrHistory[j]; }
      smooth.push(sum / (i - from + 1));
    }
    ctx.lineWidth = 1;
    ctx.strokeStyle = cssVar('--accent', '#5f9ea3');
    ctx.globalAlpha = 0.34;
    ctx.beginPath();
    for (let i = 0; i < snrHistory.length; i++) {
      const x = i * W / (snrHistory.length - 1);
      const y = H - 4 - ((snrHistory[i] - min) / span) * (H - 10);
      if (i === 0) { ctx.moveTo(x, y); } else { ctx.lineTo(x, y); }
    }
    ctx.stroke();
    ctx.globalAlpha = 1;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    for (let i = 0; i < smooth.length; i++) {
      const x = i * W / (smooth.length - 1);
      const y = H - 4 - ((smooth[i] - min) / span) * (H - 10);
      if (i === 0) { ctx.moveTo(x, y); } else { ctx.lineTo(x, y); }
    }
    ctx.stroke();
  }

  function renderStatus(status) {
    const pipeline = status.pipeline || {};
    setText('dash-mode', ED.modeLabel(status.mode) || '-');
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
    const sdrNode = document.getElementById('dash-sdr');
    if (sdrNode) {
      if (sdr.available) {
        const freq = ED.fmt(sdr.center_freq_hz / 1e6, 3) + ' MHz';
        const rate = ED.fmt(sdr.sample_rate_hz / 1e6, 2) + ' MSPS';
        // The full line wraps into the label column on a phone-width readout.
        const narrow = window.innerWidth <= 640;
        sdrNode.textContent = narrow
          ? sdr.driver + ' ' + freq
          : sdr.driver + ' @ ' + freq + ', ' + rate;
        sdrNode.title = sdr.driver + ' ' + freq + ', ' + rate;
      } else {
        sdrNode.textContent = 'unavailable';
      }
    }
    const schedule = status.schedule || {};
    // "hrit-1512 in 28m" reads as a filename; the mode is what she wants to see.
    const nextName = schedule.next_entry || '';
    const nextMode = nextName.split('-')[0].toUpperCase();
    const label = ['HRIT', 'LRIT', 'GGAK'].indexOf(nextMode) >= 0
      ? nextMode
      : nextName;
    setText('dash-next', nextName
      ? (label + ' in ' + ED.fmtDuration(schedule.seconds_to_next))
      : 'none');

    const snr = Number(pipeline.snr_db);
    if (!isNaN(snr)) {
      snrHistory.push(snr);
      if (snrHistory.length > HISTORY_LIMIT) { snrHistory.shift(); }
      drawSpark();
      renderAvg();
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

  // The daemon keeps a one-second SNR ring, so the chart opens with a curve
  // instead of an empty axis. Once seeded, the 1 Hz status poll keeps it live.
  async function seedHistory() {
    try {
      const data = await ED.getJson('/api/v1/history/snr?limit=' + HISTORY_LIMIT);
      const samples = data.samples || [];
      snrHistory.length = 0;
      for (const value of samples) {
        snrHistory.push(Number(value));
      }
      seeded = true;
      drawSpark();
      renderAvg(data.avg5_db);
    } catch (error) {
      seeded = true;  // live polling still works without history
    }
  }

  function renderAvg(fallback) {
    const node = document.getElementById('dash-avg');
    if (!node) { return; }
    const recent = snrHistory.slice(-5);
    let value = Number(fallback);
    if (recent.length > 0) {
      value = recent.reduce(function (a, b) { return a + b; }, 0) / recent.length;
    }
    node.textContent = isFinite(value) ? ED.fmt(value, 2) : '-';
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
  seedHistory();
  ED.startStatusPolling();
  ED.repeatWhileVisible(tick, 1000);
})();
