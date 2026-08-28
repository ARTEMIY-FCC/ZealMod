'use strict';
/* ZealMod Studio — вся логика окна.  Ничего никуда не отправляется: страница
   говорит только со своей же программой на 127.0.0.1. */

const $ = (s) => document.querySelector(s);
const api = async (path, opts) => {
  const r = await fetch(path, opts);
  const ct = r.headers.get('content-type') || '';
  const data = ct.includes('json') ? await r.json() : await r.text();
  if (!r.ok || (data && data.error)) throw new Error((data && data.error) || r.statusText);
  return data;
};

const S = {
  mods: [], themes: [], order: [], on: new Set(), covers: {},
  theme: '__default__', layout: 0, part: 2 * 1024 * 1024, report: null, sel: 0,
  cfg: { btn_map: [0, 1, 2, 3], menu_btn: 0, menu_hold_ms: 1500, exit_hold_ms: 1400,
         splash: 1, snd_on: 1, mute_stock: 1, lang: 'en' },
};
const kb = (n) => n >= 1048576 ? (n / 1048576).toFixed(2) + ' MB'
  : n >= 1024 ? (n / 1024).toFixed(1) + ' KB' : n + ' B';
const btnNames = () => [t('btn_up'), t('btn_down'), t('btn_left'), t('btn_right')];

/* ---------- загрузка состояния ---------- */
async function load() {
  const st = await api('/api/state?lang=' + UILANG);
  S.mods = st.modules;
  S.themes = st.themes;
  S.part = st.part;
  S.order = st.modules.map((m) => m.id);
  if (!S.on.size) st.modules.forEach((m) => { if (m.ok !== false) S.on.add(m.id); });
  $('#sub').textContent = `${st.profile.name} · ${st.bundle}`;
  if (!st.base.ok) toast(t('wrong_base'), 'bad', 9000);
  if (!st.modules.length) toast(t('no_modules'), 'bad', 15000);
  fillThemes();
  fillButtons();
  renderList();
  devices(st.device);
  loadCovers();
  build();
}

function devices(d) {
  const sel = $('#port');
  const cur = sel.value;
  sel.innerHTML = '';
  (d.ports || []).forEach((p) => {
    const o = document.createElement('option');
    o.value = p.port;
    o.textContent = p.score ? `${p.port} — ${t('is_timer')}` : p.port;
    sel.appendChild(o);
  });
  if (!d.ports || !d.ports.length) {
    const o = document.createElement('option');
    o.textContent = t('no_device');
    o.value = '';
    sel.appendChild(o);
  }
  if (cur) sel.value = cur;
  const found = (d.ports || []).some((p) => p.score);
  $('#devdot').classList.toggle('on', found);
  if (!d.esptool && !S.warnedTool) { S.warnedTool = 1; toast(t('need_esptool'), 'bad', 8000); }
}

/* ---------- список программ ---------- */
function renderList() {
  const box = $('#list');
  box.innerHTML = '';
  S.order.forEach((id) => {
    const m = S.mods.find((x) => x.id === id);
    if (!m) return;
    const el = document.createElement('div');
    el.className = 'mod' + (S.on.has(id) ? '' : ' off') + (m.ok === false ? ' bad' : '');
    el.draggable = true;
    el.dataset.id = id;
    el.innerHTML = `
      <input type="checkbox" ${S.on.has(id) ? 'checked' : ''} ${m.ok === false ? 'disabled' : ''}>
      <img src="/api/cover/${id}.png" alt="" onerror="this.style.visibility='hidden'">
      <div>
        <div class="nm">${esc(m.name)}</div>
        <div class="meta">${t(m.kind === 'game' ? 'game' : 'app')} ·
          ${t('exit_hint')} ${esc(btnArrow(m.exit_button))} ${(m.exit_hold || 1.4).toFixed(1)} s</div>
      </div>
      <div class="sz">${kb((m.code || 0) + (m.data || 0))}</div>
      ${m.ok === false ? `<div class="warnrow">${esc((m.problems || []).join('; '))}</div>` : ''}`;
    el.querySelector('input').onchange = (e) => {
      e.target.checked ? S.on.add(id) : S.on.delete(id);
      el.classList.toggle('off', !e.target.checked);
      count(); build();
    };
    dnd(el);
    box.appendChild(el);
  });
  count();
}

function btnArrow(v) {
  return ({ up: '▲', down: '▼', left: '◀', right: '▶' })[v] || v || '▲';
}
function esc(s) {
  return String(s == null ? '' : s).replace(/[&<>"]/g, (c) =>
    ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}
function count() { $('#picked').textContent = S.on.size; }

let dragId = null;
function dnd(el) {
  el.addEventListener('dragstart', () => { dragId = el.dataset.id; el.classList.add('drag'); });
  el.addEventListener('dragend', () => { el.classList.remove('drag'); dragId = null; });
  el.addEventListener('dragover', (e) => { e.preventDefault(); el.classList.add('over'); });
  el.addEventListener('dragleave', () => el.classList.remove('over'));
  el.addEventListener('drop', (e) => {
    e.preventDefault();
    el.classList.remove('over');
    if (!dragId || dragId === el.dataset.id) return;
    const from = S.order.indexOf(dragId), to = S.order.indexOf(el.dataset.id);
    S.order.splice(to, 0, S.order.splice(from, 1)[0]);
    renderList(); draw(); build();
  });
}

/* ---------- темы и настройки ---------- */
function fillThemes() {
  const sel = $('#theme');
  sel.innerHTML = '';
  S.themes.forEach((th) => {
    const o = document.createElement('option');
    o.value = th.id;
    o.textContent = th.name;
    sel.appendChild(o);
  });
  sel.value = S.theme;
  swatches();
}
function curTheme() {
  return S.themes.find((x) => x.id === S.theme) || S.themes[0] || { colors: {} };
}
function swatches() {
  const c = curTheme().colors || {};
  $('#swatches').innerHTML = ['bg_top', 'bg_bot', 'fl_bot', 'line', 'accent', 'text']
    .map((k) => `<i style="background:${c[k] || '#000'}" title="${k}"></i>`).join('');
}

function fillButtons() {
  const box = $('#btnmap');
  box.innerHTML = '';
  btnNames().forEach((name, i) => {
    const d = document.createElement('div');
    d.innerHTML = `<span>${name.split(' ')[0]}</span>`;
    const s = document.createElement('select');
    [0, 1, 2, 3].forEach((raw) => {
      const o = document.createElement('option');
      o.value = raw; o.textContent = t('button_n', { n: raw + 1 });
      s.appendChild(o);
    });
    s.value = S.cfg.btn_map[i];
    s.onchange = () => { S.cfg.btn_map[i] = +s.value; build(); };
    d.appendChild(s);
    box.appendChild(d);
  });
  const mb = $('#menubtn');
  mb.innerHTML = '';
  btnNames().forEach((name, i) => {
    const o = document.createElement('option');
    o.value = i; o.textContent = name;
    mb.appendChild(o);
  });
  mb.value = S.cfg.menu_btn;
  $('#menuholdv').textContent = t('seconds', { v: (S.cfg.menu_hold_ms / 1000).toFixed(1) });
  $('#exitholdv').textContent = t('seconds', { v: (S.cfg.exit_hold_ms / 1000).toFixed(1) });
}

/* ---------- предпросмотр ---------- */
function loadCovers() {
  S.mods.forEach((m) => {
    if (S.covers[m.id]) return;
    const img = new Image();
    img.onload = draw;
    img.src = `/api/cover/${m.id}.png`;
    S.covers[m.id] = img;
  });
}

function grad(g, x, y, w, h, a, b) {
  const lg = g.createLinearGradient(0, y, 0, y + h);
  lg.addColorStop(0, a); lg.addColorStop(1, b);
  g.fillStyle = lg; g.fillRect(x, y, w, h);
}

function draw() {
  const g = $('#preview').getContext('2d');
  const c = curTheme().colors || {};
  const chosen = S.order.filter((id) => S.on.has(id));
  g.clearRect(0, 0, 240, 240);
  grad(g, 0, 0, 240, 150, c.bg_top || '#101220', c.bg_bot || '#030306');
  grad(g, 0, 150, 240, 90, c.fl_top || '#030306', c.fl_bot || '#0e0f18');
  g.fillStyle = c.line || '#2c3042';
  g.fillRect(0, 150, 240, 1);
  g.imageSmoothingEnabled = false;
  if (!chosen.length) {
    g.fillStyle = c.text || '#fff';
    g.font = '600 15px system-ui'; g.textAlign = 'center';
    g.fillText(t('no_programs'), 120, 118);
    return;
  }
  if (S.layout === 1) return drawGrid(g, c, chosen);
  if (S.layout === 2) return drawList(g, c, chosen);
  drawFlow(g, c, chosen);
}

function cover(g, id, x, y, w, h, dim) {
  const img = S.covers[id];
  g.save();
  if (dim) g.globalAlpha = dim;
  if (img && img.complete && img.naturalWidth) g.drawImage(img, x, y, w, h);
  else { g.fillStyle = '#2a2f3a'; g.fillRect(x, y, w, h); }
  g.restore();
}

function drawFlow(g, c, ids) {
  const sel = Math.min(Math.max(S.sel | 0, 0), ids.length - 1);
  S.sel = sel;
  const quads = [];
  for (let d = 3; d >= 1; d--) { quads.push(sel - d); quads.push(sel + d); }
  quads.push(sel);
  /* повторяем то, что часы считают перспективой: соседи узкие, стоят вплотную */
  const OFF = [0, 76, 104, 130], HGT = [96, 78, 72, 68];
  const geom = (i) => {
    const d = i - sel, k = Math.min(Math.abs(d), 3);
    const h = HGT[k], w = k ? 55 : 96;
    const cx = 120 + Math.sign(d) * OFF[k];
    return { x: cx - w / 2, y: 100 - h / 2, w, h,
             dim: k === 0 ? 1 : Math.max(0.3, 0.8 - k * 0.16) };
  };
  quads.forEach((i) => {
    if (i < 0 || i >= ids.length) return;
    const q = geom(i);
    cover(g, ids[i], q.x, q.y, q.w, q.h, q.dim);
    reflect(g, ids[i], q);
  });
  const m = S.mods.find((x) => x.id === ids[sel]);
  if (!(S.hideTitle)) {
    g.font = '650 18px system-ui'; g.textAlign = 'center';
    g.fillStyle = c.shadow || '#000';
    g.fillText(m ? m.name : '', 122, 224);
    g.fillStyle = c.text || '#fff';
    g.fillText(m ? m.name : '', 120, 222);
    const n = Math.min(ids.length, 20);
    for (let i = 0; i < n; i++) {
      g.fillStyle = i === sel ? (c.accent || '#e6e6f0') : (c.text_dim || '#3c3e4a');
      g.fillRect(120 - n * 5 + i * 10, 232, 5, 3);
    }
  }
}

/* отражение: полосками, каждая следующая бледнее — так же гаснет и на часах */
function reflect(g, id, q) {
  const img = S.covers[id];
  if (!img || !img.complete || !img.naturalWidth) return;
  const rh = q.h * 0.42, n = 14, step = rh / n;
  for (let i = 0; i < n; i++) {
    const a = (1 - i / n) * 0.42 * q.dim;
    if (a <= 0.01) break;
    g.save();
    g.globalAlpha = a;
    g.beginPath(); g.rect(q.x, q.y + q.h + i * step, q.w, step + 0.6); g.clip();
    g.translate(0, 2 * (q.y + q.h)); g.scale(1, -1);
    g.drawImage(img, q.x, q.y, q.w, q.h);
    g.restore();
  }
}

function hexa(hex, a) {                                  /* #rrggbb -> rgba() */
  const h = String(hex).replace('#', '');
  const n = parseInt(h.length === 3 ? h.replace(/./g, (c) => c + c) : h, 16) || 0;
  return `rgba(${(n >> 16) & 255},${(n >> 8) & 255},${n & 255},${a})`;
}

function drawGrid(g, c, ids) {
  const sel = (S.sel | 0) % Math.max(ids.length, 1);
  const first = Math.max(0, Math.floor(sel / 3) - 1) * 3;      /* видимое окно */
  ids.slice(first, first + 9).forEach((id, i) => {
    const x = 12 + (i % 3) * 74, y = 14 + ((i / 3) | 0) * 74;
    if (first + i === sel) {
      g.fillStyle = c.accent || '#6ea8ff';
      g.fillRect(x - 3, y - 3, 72, 72);
    }
    cover(g, id, x, y, 66, 66, first + i === sel ? 1 : 0.8);
  });
  const m = S.mods.find((x) => x.id === ids[sel]);
  g.fillStyle = '#000a'; g.fillRect(0, 214, 240, 26);
  g.fillStyle = c.text || '#fff';
  g.font = '600 15px system-ui'; g.textAlign = 'center';
  g.fillText(m ? m.name : '', 120, 233);
}

function drawList(g, c, ids) {
  const sel = (S.sel | 0) % Math.max(ids.length, 1);
  const first = Math.max(0, sel - 2);
  ids.slice(first, first + 6).forEach((id, i) => {
    const y = 8 + i * 46;
    const on = first + i === sel;
    if (on) { g.fillStyle = c.accent || '#6ea8ff'; g.fillRect(6, y, 228, 40); }
    cover(g, id, 12, y + 4, 32, 32, 1);
    const m = S.mods.find((x) => x.id === id);
    g.fillStyle = on ? (c.bg_bot || '#000') : (c.text || '#fff');
    g.font = '600 15px system-ui'; g.textAlign = 'left';
    g.fillText(m ? m.name : '', 54, y + 26);
  });
}

/* ---------- сборка ---------- */
let buildTimer = null;
function build() {
  clearTimeout(buildTimer);
  buildTimer = setTimeout(doBuild, 220);
  draw();
}

async function doBuild() {
  const body = {
    modules: S.order.filter((id) => S.on.has(id)),
    theme: S.theme, layout: S.layout, cfg: S.cfg,
  };
  $('#usage').textContent = t('building');
  try {
    const r = await api('/api/build', { method: 'POST', body: JSON.stringify(body) });
    S.report = r;
    const used = r.size / S.part;
    $('#barfill').style.width = (used * 100).toFixed(1) + '%';
    $('#barfree').style.width = (100 - used * 100).toFixed(1) + '%';
    $('#usage').textContent = t('usage', {
      size: kb(r.size), part: kb(S.part), free: kb(r.free),
      n: r.modules.length, pages: r.pages });
    $('#flash').disabled = false;
    $('#export').disabled = false;
  } catch (e) {
    S.report = null;
    $('#usage').textContent = t('cant_build', { err: e.message });
    $('#barfill').style.width = '100%';
    $('#flash').disabled = true;
    $('#export').disabled = true;
    toast(e.message, 'bad', 7000);
  }
}

/* ---------- долгие дела ---------- */
let poll = null;
function watch(title) {
  $('#logtitle').textContent = title;
  $('#logtext').textContent = '';
  $('#log').classList.remove('hidden');
  clearInterval(poll);
  poll = setInterval(async () => {
    const p = await api('/api/progress');
    $('#logtext').textContent = p.lines.join('\n');
    $('#logtext').scrollTop = 1e9;
    if (p.done) {
      clearInterval(poll);
      toast(t(p.ok ? 'done' : 'failed', { job: title }), p.ok ? 'good' : 'bad', 6000);
    }
  }, 400);
}

/* ---------- мелочи ---------- */
let toastTimer = null;
function toast(text, kind, ms) {
  const t = $('#toast');
  t.textContent = text;
  t.className = 'toast ' + (kind || '');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.add('hidden'), ms || 3500);
}

async function upload(file) {
  const buf = await file.arrayBuffer();
  try {
    const r = await api('/api/import?name=' + encodeURIComponent(file.name),
                        { method: 'POST', body: buf });
    if (r.kind === 'module') {
      S.on.add(r.id);
      toast(r.ok ? t('installed', { name: r.name }) : `${r.name}: ${r.problems.join('; ')}`,
            r.ok ? 'good' : 'bad', 7000);
    } else {
      S.theme = r.id;
      toast(t('theme_added'), 'good');
    }
    await load();
  } catch (e) {
    toast(e.message, 'bad', 7000);
  }
}

function pick(accept, cb) {
  const i = document.createElement('input');
  i.type = 'file'; i.accept = accept; i.multiple = true;
  i.onchange = () => Array.from(i.files).forEach(cb);
  i.click();
}

function setLayout(v) {
  S.layout = +v;
  [...$('#layout').children].forEach((b) => b.classList.toggle('on', +b.dataset.v === S.layout));
}

/* ---------- события ---------- */
$('#uilang').onchange = async (e) => {          /* язык окна */
  applyLang(e.target.value);
  fillButtons();
  renderList();
  await load();
};
$('#modlang').onchange = (e) => { S.cfg.lang = e.target.value; build(); };
$('#theme').onchange = (e) => {
  S.theme = e.target.value;
  const th = curTheme();                       /* у темы свой вид меню */
  if (th && th.layout != null) setLayout(th.layout);
  swatches();
  build();
};

$('#layout').onclick = (e) => {
  const b = e.target.closest('button');
  if (!b) return;
  setLayout(b.dataset.v);
  build();
};
$('#menubtn').onchange = (e) => { S.cfg.menu_btn = +e.target.value; build(); };
$('#menuhold').oninput = (e) => {
  S.cfg.menu_hold_ms = +e.target.value;
  $('#menuholdv').textContent = t('seconds', { v: (S.cfg.menu_hold_ms / 1000).toFixed(1) });
  build();
};
$('#exithold').oninput = (e) => {
  S.cfg.exit_hold_ms = +e.target.value;
  $('#exitholdv').textContent = t('seconds', { v: (S.cfg.exit_hold_ms / 1000).toFixed(1) });
  build();
};
['splash', 'snd', 'mute'].forEach((k) => {
  const key = { splash: 'splash', snd: 'snd_on', mute: 'mute_stock' }[k];
  $('#' + k).onchange = (e) => { S.cfg[key] = e.target.checked ? 1 : 0; build(); };
});
$('#allon').onclick = () => {
  S.mods.forEach((m) => { if (m.ok !== false) S.on.add(m.id); });
  renderList(); build();
};
$('#alloff').onclick = () => { S.on.clear(); renderList(); build(); };
$('#addmod').onclick = () => pick('.zm', upload);
$('#addtheme').onclick = () => pick('.zt', upload);
$('#rescan').onclick = async () => devices(await api('/api/devices'));
$('#export').onclick = () => { location.href = '/api/image?name=zealmod.gbl'; };
$('#flash').onclick = async () => {
  if (!S.report) return toast(t('not_built'), 'bad');
  try {
    await api('/api/flash', { method: 'POST', body: JSON.stringify({ port: $('#port').value }) });
    watch(t('flashing'));
  } catch (e) { toast(e.message, 'bad', 6000); }
};
$('#backup').onclick = async () => {
  try {
    await api('/api/backup', { method: 'POST', body: JSON.stringify({ port: $('#port').value }) });
    watch(t('backup_job'));
  } catch (e) { toast(e.message, 'bad', 6000); }
};
$('#stock').onclick = async () => {
  if (!confirm(t('confirm_stock'))) return;
  try {
    await api('/api/stock', { method: 'POST', body: JSON.stringify({ port: $('#port').value }) });
    watch(t('stock_job'));
  } catch (e) { toast(e.message, 'bad', 6000); }
};
$('#logclose').onclick = () => $('#log').classList.add('hidden');
$('#preview').onclick = (e) => {
  const r = e.target.getBoundingClientRect();
  const n = S.order.filter((id) => S.on.has(id)).length;
  S.sel = ((S.sel | 0) + (e.clientX - r.left > r.width / 2 ? 1 : -1) + n) % Math.max(n, 1);
  draw();
};

window.addEventListener('dragover', (e) => { e.preventDefault(); $('#drop').classList.remove('hidden'); });
window.addEventListener('dragleave', (e) => { if (e.relatedTarget === null) $('#drop').classList.add('hidden'); });
window.addEventListener('drop', (e) => {
  e.preventDefault();
  $('#drop').classList.add('hidden');
  Array.from(e.dataTransfer.files).forEach(upload);
});

setInterval(async () => {
  try { devices(await api('/api/devices')); } catch (e) { /* окно ещё живо */ }
}, 4000);

applyLang(UILANG);
$('#uilang').value = UILANG;
$('#modlang').value = S.cfg.lang;
load().catch((e) => toast(t('startup_failed', { err: e.message }), 'bad', 12000));
