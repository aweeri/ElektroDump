/* ElektroDump — schedule editor (M9). */
'use strict';

(function () {
  let model = {
    enabled: true,
    timezone: 'UTC',
    default_mode: 'ggak',
    entries: []
  };

  function blankEntry() {
    return {
      name: '', start: '06:00', end: '06:20', mode: 'lrit',
      priority: 0, frequency_hz: 0, symbol_rate: 0, sample_rate_hz: 0
    };
  }

  function render() {
    document.getElementById('sched-enabled').checked = !!model.enabled;
    document.getElementById('sched-timezone').value = model.timezone || 'UTC';
    document.getElementById('sched-mode').value = model.default_mode || 'ggak';

    const body = document.getElementById('sched-rows');
    body.innerHTML = model.entries.map(function (entry, index) {
      return '<tr>' +
        '<td><input data-k="name" data-i="' + index + '" value="' + ED.esc(entry.name) + '"></td>' +
        '<td><input data-k="start" data-i="' + index + '" value="' + ED.esc(entry.start) + '" size="6"></td>' +
        '<td><input data-k="end" data-i="' + index + '" value="' + ED.esc(entry.end) + '" size="6"></td>' +
        '<td><select data-k="mode" data-i="' + index + '">' +
        '<option value="lrit"' + (entry.mode === 'lrit' ? ' selected' : '') + '>lrit</option>' +
        '<option value="hrit"' + (entry.mode === 'hrit' ? ' selected' : '') + '>hrit</option>' +
        '</select></td>' +
        '<td><input data-k="priority" data-i="' + index + '" type="number" value="' + Number(entry.priority || 0) + '" size="3"></td>' +
        '<td><input data-k="frequency_hz" data-i="' + index + '" type="number" value="' + Number(entry.frequency_hz || 0) + '"></td>' +
        '<td><button class="danger" data-remove="' + index + '">remove</button></td>' +
        '</tr>';
    }).join('');

    for (const input of body.querySelectorAll('input, select')) {
      input.addEventListener('input', function () {
        const index = Number(input.dataset.i);
        const key = input.dataset.k;
        let value = input.value;
        if (key === 'priority' || key === 'frequency_hz') {
          value = Number(value) || 0;
        }
        model.entries[index][key] = value;
      });
    }
    for (const button of body.querySelectorAll('button[data-remove]')) {
      button.addEventListener('click', function () {
        model.entries.splice(Number(button.dataset.remove), 1);
        render();
      });
    }
  }

  async function load() {
    try {
      const data = await ED.getJson('/api/v1/schedule');
      if (data.schedule) {
        model = data.schedule;
        if (!Array.isArray(model.entries)) { model.entries = []; }
      }
      render();
      const state = data.state || {};
      document.getElementById('sched-state').textContent = state.active_entry
        ? ('active: ' + state.active_entry + ' · next: ' +
           (state.next_entry || '—') + ' in ' +
           ED.fmtDuration(state.seconds_to_next))
        : 'no active entry';
    } catch (error) {
      ED.toast(error.message);
    }
  }

  async function save() {
    model.enabled = document.getElementById('sched-enabled').checked;
    model.timezone = document.getElementById('sched-timezone').value;
    model.default_mode = document.getElementById('sched-mode').value;
    const result = await ED.putJson('/api/v1/schedule', { schedule: model });
    const output = document.getElementById('sched-result');
    if (result.ok) {
      output.textContent = 'saved';
      output.className = 'muted';
      if (result.data.schedule) {
        model = result.data.schedule;
        render();
      }
    } else {
      const fields = (result.data.fields || [])
        .map(function (f) { return f.path + ': ' + f.message; }).join('; ');
      output.textContent = 'error: ' + (result.data.error || result.status) +
        (fields ? ' — ' + fields : '');
      output.className = '';
    }
  }

  document.getElementById('sched-add').addEventListener('click', function () {
    model.entries.push(blankEntry());
    render();
  });
  document.getElementById('sched-save').addEventListener('click', save);
  document.getElementById('sched-reload').addEventListener('click', load);

  ED.setPage('schedule');
  ED.startStatusPolling();
  load();
})();
