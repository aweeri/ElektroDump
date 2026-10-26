/* ElektroDump — GGAK science page (M9). */
'use strict';

(function () {
  let selected = 0;
  let populated = false;

  function updatePlot() {
    document.getElementById('ggak-plot').src =
      '/api/v1/ggak/plots/' + selected + '.svg?_=' + Date.now();
    document.getElementById('ggak-csv').href = '/api/v1/ggak/csv?channel=' + selected;
  }

  function renderOverview(data) {
    const channels = data.channels || [];
    const select = document.getElementById('ggak-channel');
    if (!populated && channels.length > 0) {
      populated = true;
      select.innerHTML = channels
        .filter(function (channel) { return channel.present; })
        .map(function (channel) {
          return '<option value="' + channel.id + '">' + ED.esc(channel.name) +
            '</option>';
        }).join('');
      selected = Number(select.value) || 0;
      select.addEventListener('change', function () {
        selected = Number(select.value) || 0;
        updatePlot();
      });
      updatePlot();
    }

    const stats = data.stats || {};
    document.getElementById('ggak-status').textContent = data.available
      ? ('SKL live · ' + (stats.skl_frames || 0) + ' frames, ' +
         (stats.records || 0) + ' records')
      : 'no GGAK science data yet';

    const rows = document.getElementById('ggak-rows');
    rows.innerHTML = channels.map(function (channel) {
      return '<tr><td class="mono">' + ED.esc(channel.name) + '</td>' +
        '<td>' + ED.esc(channel.unit || '—') + '</td>' +
        '<td class="mono">' + (channel.has_value ? ED.fmt(channel.latest, 2) : '—') + '</td>' +
        '<td class="mono muted">' + ED.esc(channel.time || '—') + '</td>' +
        '<td class="mono">' + (channel.count || 0) + '</td></tr>';
    }).join('');
  }

  async function tick() {
    try {
      renderOverview(await ED.getJson('/api/v1/ggak'));
    } catch (error) {
      ED.toast(error.message);
    }
  }

  ED.setPage('ggak');
  ED.startStatusPolling();
  ED.repeatWhileVisible(tick, 2000);
})();
