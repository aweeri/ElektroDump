/* ElektroDump - pass timetable. Read-only: the daemon owns the schedule. */
'use strict';

(function () {
  const svg = document.getElementById('tl-svg');
  const list = document.getElementById('tl-list');
  const rows = document.getElementById('sched-rows');
  const caption = document.getElementById('tl-cap');
  const state = document.getElementById('sched-state');

  const NS = 'http://www.w3.org/2000/svg';
  const W = 1000;
  // Vertical rhythm: HRIT lane above the axis, hour labels just under it, LRIT
  // lane below them, so nothing overlaps at any zoom.
  const AXIS_Y = 80;
  const LANE = {
    hrit: { y: 52, h: 12 },
    lrit: { y: 108, h: 12 }
  };

  function el(name, attrs) {
    const node = document.createElementNS(NS, name);
    for (const key in attrs) {
      node.setAttribute(key, attrs[key]);
    }
    return node;
  }

  function css(name, fallback) {
    const v = getComputedStyle(document.documentElement)
      .getPropertyValue(name).trim();
    return v || fallback;
  }

  // "HH:MM" -> minutes past midnight.
  function minutes(text) {
    const parts = String(text || '').split(':');
    const h = Number(parts[0]);
    const m = Number(parts[1]);
    if (!isFinite(h) || !isFinite(m)) { return null; }
    return h * 60 + m;
  }

  function xOf(min) {
    return (min / 1440) * W;
  }

  function draw(entries) {
    while (svg.firstChild) { svg.removeChild(svg.firstChild); }

    const ink = css('--fg', '#14171a');
    const muted = css('--muted', '#6b7280');
    const rule = css('--rule', '#dbdfe1');
    const accent = css('--accent', '#1c6469');
    const now = css('--error', '#b0392c');

    // hour graticule and labels
    for (let h = 0; h <= 24; h += 3) {
      const x = xOf(h * 60);
      svg.appendChild(el('line', {
        x1: x, y1: AXIS_Y, x2: x, y2: AXIS_Y + 7,
        stroke: ink, 'stroke-width': 1
      }));
      const label = el('text', {
        x: h === 24 ? x - 14 : x, y: AXIS_Y + 18,
        fill: muted, 'font-size': 9.5, 'letter-spacing': '0.06em',
        'font-family': 'Plex Mono, monospace'
      });
      label.textContent = h < 10 ? '0' + h : String(h);
      if (h === 0) { label.setAttribute('x', x + 2); }
      svg.appendChild(label);
    }

    // the axis itself
    svg.appendChild(el('line', {
      x1: 0, y1: AXIS_Y + 0.5, x2: W, y2: AXIS_Y + 0.5,
      stroke: ink, 'stroke-width': 1
    }));

    // windows
    entries.forEach(function (entry) {
      const lane = LANE[entry.mode] || LANE.lrit;
      const a = minutes(entry.start);
      let b = minutes(entry.end);
      if (a === null || b === null) { return; }
      if (b <= a) { b += 1440; }
      const x = xOf(a % 1440);
      const w = Math.max(2, xOf(b - a));
      const isHrit = entry.mode === 'hrit';
      svg.appendChild(el('rect', {
        x: x, y: lane.y, width: w, height: lane.h,
        fill: isHrit ? accent : 'none',
        stroke: accent, 'stroke-width': isHrit ? 0 : 1,
        'fill-opacity': isHrit ? 0.85 : 0
      }));
    });

    // "now" marker
    const nowDate = new Date();
    const nowMin = nowDate.getUTCHours() * 60 + nowDate.getUTCMinutes();
    const nx = xOf(nowMin);
    svg.appendChild(el('line', {
      x1: nx, y1: 34, x2: nx, y2: 132,
      stroke: now, 'stroke-width': 1, 'stroke-dasharray': '2 3'
    }));
    svg.appendChild(el('circle', { cx: nx, cy: AXIS_Y, r: 3, fill: now }));
    const stamp = el('text', {
      x: Math.min(W - 34, Math.max(2, nx + 4)), y: 28,
      fill: now, 'font-size': 10, 'letter-spacing': '0.06em',
      'font-family': 'Plex Mono, monospace'
    });
    stamp.textContent = (nowDate.getUTCHours() < 10 ? '0' : '') +
      nowDate.getUTCHours() + ':' +
      (nowDate.getUTCMinutes() < 10 ? '0' : '') + nowDate.getUTCMinutes();
    svg.appendChild(stamp);

    // a hairline at midnight edges so the plot reads as a bounded day
    svg.appendChild(el('line', {
      x1: 0.5, y1: 40, x2: 0.5, y2: 132, stroke: rule, 'stroke-width': 1
    }));
    svg.appendChild(el('line', {
      x1: W - 0.5, y1: 40, x2: W - 0.5, y2: 132, stroke: rule, 'stroke-width': 1
    }));
  }

  function drawList(entries) {
    const byMode = { hrit: [], lrit: [] };
    entries.forEach(function (entry) {
      (byMode[entry.mode] || byMode.lrit).push(entry);
    });
    list.innerHTML = ['hrit', 'lrit'].map(function (mode) {
      const items = byMode[mode];
      if (!items.length) { return ''; }
      return '<div><dt>' + mode.toUpperCase() + '</dt><dd>' +
        items.map(function (e) { return e.start + '–' + e.end; }).join(' &nbsp; ') +
        '</dd></div>';
    }).join('');
  }

  function duration(entry) {
    const a = minutes(entry.start);
    const b = minutes(entry.end);
    if (a === null || b === null) { return '—'; }
    return String((b > a ? b - a : b + 1440 - a));
  }

  function drawTable(entries) {
    rows.innerHTML = entries.map(function (entry) {
      return '<tr>' +
        '<td class="mono">' + ED.esc(entry.name) + '</td>' +
        '<td class="mono">' + ED.esc(entry.start) + '</td>' +
        '<td class="mono">' + ED.esc(entry.end) + '</td>' +
        '<td class="mono num">' + duration(entry) + '</td>' +
        '<td>' + ED.esc(entry.mode) + '</td>' +
        '</tr>';
    }).join('');
  }

  async function load() {
    try {
      const data = await ED.getJson('/api/v1/schedule');
      const schedule = data.schedule || {};
      const entries = Array.isArray(schedule.entries) ? schedule.entries : [];
      draw(entries);
      drawList(entries);
      drawTable(entries);

      const st = data.state || {};
      caption.textContent = 'Times are UTC. ' + entries.length +
        ' windows a day, ' + (schedule.enabled === false ? 'schedule disabled' : 'schedule enabled') + '.';
      state.textContent = st.active_entry
        ? ('receiving ' + st.active_entry + '; next ' + (st.next_entry || '-') +
           ' in ' + ED.fmtDuration(st.seconds_to_next))
        : ('idle; next ' + (st.next_entry || '-') + ' in ' +
           ED.fmtDuration(st.seconds_to_next));
    } catch (error) {
      ED.toast(error.message);
    }
  }

  ED.setPage('schedule');
  ED.startStatusPolling();
  load();
  // The "now" marker has to keep up without re-fetching the schedule every second.
  setInterval(function () {
    if (document.visibilityState === 'visible') { load(); }
  }, 30000);
})();
