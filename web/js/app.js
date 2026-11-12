/* ElektroDump — shared shell: nav, status pill, formatting, 1 Hz polling (M9). */
'use strict';

window.ED = window.ED || {};

ED.pages = [
  { key: 'index', href: '/', label: 'Dashboard' },
  { key: 'gallery', href: '/gallery', label: 'Gallery' },
  { key: 'schedule', href: '/schedule', label: 'Schedule' },
  { key: 'ggak', href: '/ggak', label: 'GGAK' },
  { key: 'system', href: '/system', label: 'System' },
  { key: 'waterfall', href: '/waterfall', label: 'Waterfall' },
  { key: 'constellation', href: '/constellation', label: 'Constellation' }
];

ED.esc = function (value) {
  const text = value === null || value === undefined ? '' : String(value);
  const entities = {
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&#39;'
  };
  return text.replace(/[&<>"']/g, function (ch) { return entities[ch]; });
};

ED.fmt = function (value, digits) {
  if (value === null || value === undefined || isNaN(Number(value))) {
    return '—';
  }
  return Number(value).toFixed(digits === undefined ? 2 : digits);
};

ED.fmtDuration = function (seconds) {
  const total = Math.max(0, Math.floor(Number(seconds) || 0));
  const days = Math.floor(total / 86400);
  const hours = Math.floor((total % 86400) / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  const secs = total % 60;
  if (days > 0) {
    return days + 'd ' + hours + 'h';
  }
  if (hours > 0) {
    return hours + 'h ' + minutes + 'm';
  }
  if (minutes > 0) {
    return minutes + 'm ' + secs + 's';
  }
  return secs + 's';
};

let toastTimer = null;
ED.toast = function (message) {
  let node = document.getElementById('toast');
  if (!node) {
    node = document.createElement('div');
    node.id = 'toast';
    document.body.appendChild(node);
  }
  node.textContent = message;
  node.classList.add('show');
  if (toastTimer) {
    clearTimeout(toastTimer);
  }
  toastTimer = setTimeout(function () { node.classList.remove('show'); }, 5000);
};

ED.setPage = function (key) {
  document.body.dataset.page = key;
  const header = document.getElementById('topbar');
  if (!header) {
    return;
  }
  let nav = '<nav class="pages">';
  for (const page of ED.pages) {
    nav += '<a href="' + page.href + '"' +
      (page.key === key ? ' class="active"' : '') + '>' +
      ED.esc(page.label) + '</a>';
  }
  nav += '</nav>';
  header.innerHTML =
    '<h1>Elektro<span>Dump</span></h1>' + nav +
    '<div class="status-pill">' +
    '<span><span class="dot nosync" id="pill-dot"></span> ' +
    '<span id="pill-mode">…</span></span>' +
    '<span id="pill-lock" class="lock nosync">not synced</span>' +
    '<span id="pill-snr">SNR —</span>' +
    '<span id="pill-uptime">up —</span>' +
    '</div>';
};

// One place decides the sync state, SatDump style: red = not synced, amber =
// syncing (demodulator tracking, no frames yet), green = synced (frames
// arriving). Every page that shows lock state goes through this so the views
// cannot drift apart.
ED.lockState = function (pipeline) {
  pipeline = pipeline || {};
  const state = pipeline.locked
    ? 'synced'
    : (pipeline.demod_locked ? 'syncing' : 'nosync');
  return {
    state: state,
    text: { synced: 'synced', syncing: 'syncing', nosync: 'not synced' }[state],
  };
};

ED.lockHtml = function (pipeline) {
  const lock = ED.lockState(pipeline);
  return '<span class="lock ' + lock.state + '">' + lock.text + '</span>';
};

ED.applyStatus = function (status) {
  const pipeline = status.pipeline || {};
  const setText = function (id, value) {
    const node = document.getElementById(id);
    if (node) { node.textContent = value; }
  };
  setText('pill-mode', status.mode || '—');
  setText('pill-snr', 'SNR ' + ED.fmt(pipeline.snr_db, 1) + ' dB');
  setText('pill-uptime', 'up ' + ED.fmtDuration(status.uptime_seconds));
  const lock = ED.lockState(pipeline);
  const dot = document.getElementById('pill-dot');
  if (dot) {
    dot.className = 'dot ' + lock.state;
  }
  const lockNode = document.getElementById('pill-lock');
  if (lockNode) {
    lockNode.textContent = lock.text;
    lockNode.className = 'lock ' + lock.state;
  }
};

// Poll the shared status endpoint at 1 Hz, skipping when the tab is hidden.
ED.startStatusPolling = function () {
  const tick = async function () {
    if (document.visibilityState !== 'visible') {
      return;
    }
    try {
      ED.applyStatus(await ED.getJson('/api/v1/status'));
    } catch (error) {
      /* leave the last value in place */
    }
  };
  tick();
  setInterval(tick, 1000);
};

// Generic visibility-gated repeater: runs `fn` every `ms` while visible.
ED.repeatWhileVisible = function (fn, ms) {
  const tick = function () {
    if (document.visibilityState === 'visible') {
      fn();
    }
  };
  fn();
  setInterval(tick, ms);
};
