/* ElektroDump — thin REST helpers (M9). No external dependencies. */
'use strict';

window.ED = window.ED || {};

ED.getJson = async function (path) {
  const response = await fetch(path, {
    headers: { 'Accept': 'application/json' },
    cache: 'no-store'
  });
  if (!response.ok) {
    throw new Error(path + ' -> HTTP ' + response.status);
  }
  return response.json();
};

ED.putJson = async function (path, body) {
  const response = await fetch(path, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  let data = {};
  try { data = await response.json(); } catch (error) { data = {}; }
  return { status: response.status, ok: response.ok, data: data };
};

ED.postJson = async function (path, body) {
  const response = await fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body || {})
  });
  let data = {};
  try { data = await response.json(); } catch (error) { data = {}; }
  return { status: response.status, ok: response.ok, data: data };
};

ED.getBlob = async function (path) {
  const response = await fetch(path, { cache: 'no-store' });
  if (!response.ok) {
    return null;
  }
  return response.blob();
};

ED.headOk = async function (path) {
  try {
    const response = await fetch(path, { cache: 'no-store' });
    return response.ok;
  } catch (error) {
    return false;
  }
};
