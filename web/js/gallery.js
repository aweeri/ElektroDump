/* ElektroDump - product gallery (M9). */
'use strict';

(function () {
  async function load() {
    const type = document.getElementById('filter-type').value;
    const date = document.getElementById('filter-date').value;
    const limit = document.getElementById('filter-limit').value || '60';
    let query = '/api/v1/products?limit=' + encodeURIComponent(limit);
    if (type) { query += '&type=' + encodeURIComponent(type); }
    if (date) { query += '&date=' + encodeURIComponent(date); }

    const grid = document.getElementById('gallery');
    try {
      const data = await ED.getJson(query);
      const products = data.products || [];
      document.getElementById('gallery-count').textContent =
        products.length + ' product(s)';
      if (products.length === 0) {
        grid.innerHTML = '<p class="empty">No products stored yet.</p>';
        return;
      }
      grid.innerHTML = products.map(function (product) {
        return '<div class="thumb" data-id="' + ED.esc(product.id) + '">' +
          '<img loading="lazy" src="/api/v1/products/' +
          encodeURIComponent(product.id) + '/image" alt="' + ED.esc(product.product) + '">' +
          '<div class="meta"><div class="name">' + ED.esc(product.product) + '</div>' +
          '<div class="muted">' + ED.esc(product.created) + '</div>' +
          '<div class="muted">' + product.width + '×' + product.height +
          (product.partial ? ' · partial' : '') + '</div></div></div>';
      }).join('');
      for (const node of grid.querySelectorAll('.thumb')) {
        node.addEventListener('click', function () { detail(node.dataset.id); });
      }
    } catch (error) {
      grid.innerHTML = '<p class="empty">Failed to load products.</p>';
      ED.toast(error.message);
    }
  }

  async function detail(id) {
    const panel = document.getElementById('gallery-detail');
    try {
      const meta = await ED.getJson('/api/v1/products/' + encodeURIComponent(id));
      panel.innerHTML = '<h2>' + ED.esc(meta.product) + '</h2>' +
        '<img src="/api/v1/products/' + encodeURIComponent(id) + '/image" alt="">' +
        '<pre>' + ED.esc(JSON.stringify(meta, null, 2)) + '</pre>';
    } catch (error) {
      ED.toast(error.message);
    }
  }

  document.getElementById('filter-apply').addEventListener('click', load);
  document.getElementById('filter-type').addEventListener('change', load);
  ED.setPage('gallery');
  ED.startStatusPolling();
  load();
})();
