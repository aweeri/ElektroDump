/* ElektroDump — dashboard widgets (M9). */
'use strict';

(function () {
  const snrHistory = [];
  let lastProductId = null;

  function setText(id, value) {
    const node = document.getElementById(id);
    if (node) { node.textContent = value; }
  }

  function drawSpark() {
    const canvas = document.getElementById('spark');
    if (!canvas) { return; }
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    if (snrHistory.length < 2) { return; }
    const min = Math.min.apply(null, snrHistory);
    const max = Math.max.apply(null, snrHistory);
    const span = (max - min) || 1;
    ctx.strokeStyle = '#4fd1c5';
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    for (let i = 0; i < snrHistory.length; i++) {
      const x = i * canvas.width / (snrHistory.length - 1);
      const y = canvas.height - 3 - ((snrHistory[i] - min) / span) *
        (canvas.height - 6);
      if (i === 0) { ctx.moveTo(x, y); } else { ctx.lineTo(x, y); }
    }
    ctx.stroke();
  }

  function renderStatus(status) {
    const pipeline = status.pipeline || {};
    setText('dash-mode', status.mode || '—');
    setText('dash-satellite', status.satellite || '—');
    setText('dash-version', status.version || '—');
    setText('dash-state', pipeline.state || '—');
    setText('dash-snr', ED.fmt(pipeline.snr_db, 2) + ' dB');
    setText('dash-ber', pipeline.locked ? ED.fmt(pipeline.ber, 6) : '—');
    const lockNode = document.getElementById('dash-lock');
    if (lockNode) {
      lockNode.innerHTML = ED.lockHtml(pipeline);
    }
    setText('dash-cadus', String(pipeline.cadus_total || 0) + ' (' +
      String(pipeline.cadus_bad || 0) + ' bad)');
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
    if (!meta) {
      caption.textContent = 'no product yet';
      return;
    }
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
