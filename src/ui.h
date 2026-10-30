#pragma once

static const char INDEX_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Files</title>
<style>
  :root { --bg:#12141c; --card:#1c2030; --line:#2c3247; --acc:#4f8cff; --txt:#e6e9f2; --dim:#8b93ab; }
  * { box-sizing:border-box; margin:0; padding:0; }
  body { font-family:'Segoe UI',system-ui,sans-serif; background:var(--bg); color:var(--txt); padding:20px; }
  .wrap { max-width:900px; margin:0 auto; }
  h1 { font-size:1.4rem; margin-bottom:4px; }
  .sub { color:var(--dim); font-size:.85rem; margin-bottom:16px; }
  .card { background:var(--card); border:1px solid var(--line); border-radius:12px; padding:16px; margin-bottom:16px; }
  .row { display:flex; gap:10px; flex-wrap:wrap; align-items:center; }
  button, .btn, select { background:var(--acc); color:#fff; border:none; border-radius:8px;
    padding:9px 14px; font-size:.9rem; cursor:pointer; text-decoration:none; display:inline-block; }
  button.sec { background:#2a3149; }
  #upIn { display:none; }
  table { width:100%; border-collapse:collapse; font-size:.9rem; }
  th { text-align:left; color:var(--dim); font-weight:600; padding:8px 6px; border-bottom:1px solid var(--line); }
  td { padding:8px 6px; border-bottom:1px solid var(--line); }
  tr:hover td { background:#232838; }
  .name { color:var(--acc); cursor:pointer; word-break:break-all; }
  .dir .nm { color:#ffd35c; font-weight:600; }
  .dim { color:var(--dim); }
  .tag { display:inline-block; background:#2a3149; color:var(--dim); border-radius:6px; padding:2px 7px; font-size:.75rem; }
  .ok { color:#7ee08a; } .bad { color:#ff7a7a; }
  .pending { opacity:.55; }
  #bar { height:6px; background:var(--line); border-radius:3px; overflow:hidden; margin-top:8px; }
  #bar div { height:100%; width:0; background:var(--acc); transition:width .2s; }
  .crumbs span { cursor:pointer; color:var(--acc); }
  .crumbs .sep { color:var(--dim); margin:0 4px; }
  #stat { margin-left:auto; color:var(--dim); font-size:.8rem; }
  select { background:#2a3149; }
  @media(max-width:600px){ .hide-m{display:none} }
</style>
</head>
<body>
<div class="wrap">
  <h1>&#128190; ESP32 &mdash; Файловая система</h1>
  <div class="sub">API: <code>/api/cards</code>, <code>/api/files</code>, <code>/api/sizes</code>, <code>/api/append</code>, <code>/api/delete</code>, <code>/api/file/info</code>, <code>/download</code>, <code>/upload</code></div>

  <div class="card">
    <div class="row">
      <select id="cardSel" onchange="changeCard(this.value)"></select>
      <label class="btn" style="position:relative">Загрузить файл
        <input id="upIn" type="file" multiple>
      </label>
      <button class="sec" onclick="refresh()">&#8635; Обновить</button>
      <span id="stat">…</span>
    </div>
    <div class="crumbs" id="crumbs" style="margin-top:12px"></div>
    <div id="bar"><div></div></div>
    <span id="btxt" class="dim"></span>
  </div>

  <div class="card">
    <table>
      <thead><tr><th>&#128193; Путь</th><th class="hide-m">Размер</th><th class="hide-m">Инфо</th><th>Действия</th></tr></thead>
      <tbody id="list"><tr><td colspan="4" class="dim">Загрузка…</td></tr></tbody>
    </table>
  </div>
</div>

<script>
// Быстрая загрузка: /api/files отдаёт только имена и тип объектов (один readdir-проход,
// никаких stat/open на каждый файл со стороны карты), поэтому список появляется мгновенно.
// Размеры файлов и количество объектов в папках доматываются плавно, порциями по BATCH,
// лёгкими запросами /api/sizes (это именно "плавное" получение, как и задумано).
let rootData = null;      // кэш содержимого корня
let rootComplete = false; // размеры корня уже целиком получены
let curPath = '/';
let curItems = [];
let card = 0;
let loadToken = 0;        // инкрементируется при каждой смене контекста — отменяет устаревшие подгрузки
const rowsByName = new Map(); // name -> {dir, sizeEl, subEl, item} для точечного обновления
const BATCH = 24;

const fmt = b => b == null ? '…'
  : b < 1024 ? b + ' Б'
  : b < 1048576 ? (b/1024).toFixed(1) + ' КБ'
  : (b/1048576).toFixed(2) + ' МБ';

const escAttr = s => String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;')
  .replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&#39;');

async function getJson(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error('HTTP ' + r.status);
  const d = await r.json();
  if (d.error) throw new Error(d.error);
  return d;
}

async function loadCards() {
  const sel = document.getElementById('cardSel');
  try {
    const d = await getJson('/api/cards');
    sel.innerHTML = d.cards.map(c =>
      `<option value="${c.index}" ${c.ready ? '' : 'disabled'}>` +
      `${escAttr(c.name)} ${c.ready ? '✓' : '(нет)'}</option>`).join('');
    if (sel.querySelector('option[value="0"]')) sel.value = 0;
  } catch (e) {
    sel.innerHTML = '<option>—</option>';
  }
}

function cardName() {
  const o = document.getElementById('cardSel').selectedOptions[0];
  return o ? o.textContent.replace(/[✓]$/,'').trim() : '?';
}

async function loadDir(path) {
  const token = ++loadToken;
  const list = document.getElementById('list');
  list.innerHTML = '<tr><td colspan="4" class="dim">Загрузка…</td></tr>';
  document.getElementById('stat').textContent = '';
  try {
    let d;
    for (let attempt = 0; ; attempt++) {           // 1 повтор при сбое транспорта
      try { d = await getJson(`/api/files?card=${card}&dir=${encodeURIComponent(path || '/')}`); break; }
      catch (e) { if (attempt >= 1) throw e; }
    }
    if (token !== loadToken) return;
    curPath = (d.dir && d.dir.length) ? d.dir : '/';
    curItems = (d.children || []).map(k => ({
      name: k.name, path: k.path, dir: !!k.dir,
      size: null,                                  // размеры придут позже, порциями
      childCount: k.dir ? null : undefined
    }));
    if (curPath === '/') rootData = curItems;
    render();
    fillSizes(token, curPath);
  } catch (e) {
    if (token !== loadToken) return;
    list.innerHTML = '<tr><td colspan="4" class="bad">Ошибка: ' + e.message + '</td></tr>';
  }
}

// Плавная (постепенная) загрузка размеров: порциями по BATCH, каждый ответ
// раскрашивает нужные ячейки сразу — интерфейс оживает чаулами, а не блокируется.
async function fillSizes(token, dir, start = 0) {
  if (token !== loadToken) return;
  let d;
  try {
    d = await getJson(`/api/sizes?card=${card}&dir=${encodeURIComponent(dir || '/')}&start=${start}&count=${BATCH}`);
  } catch (e) {
    if (token === loadToken)
      document.getElementById('stat').textContent = 'размеры недоступны: ' + e.message;
    return;
  }
  if (token !== loadToken) return;

  for (const it of (d.items || [])) {
    const t = rowsByName.get(it.name);
    if (!t || !t.item) continue;                    // объект исчез/папка сменилась — пропустить
    if (it.err) { if (t.dir && t.subEl) t.subEl.textContent = '—'; continue; }
    if (t.dir) {
      if (typeof it.children === 'number' && it.children >= 0) {
        t.item.childCount = it.children;
        if (t.subEl) t.subEl.textContent = it.children + ' об.';
      }
    } else if (typeof it.size === 'number') {
      t.item.size = it.size;
      if (t.sizeEl) t.sizeEl.textContent = fmt(it.size);
    }
  }
  updStat();

  if (d.more != null && d.more >= 0) {
    setTimeout(() => fillSizes(token, dir, d.more), 0);   // следующая порция
  } else if (dir === '/' && token === loadToken) {
    rootComplete = true;                            // кэш корня полностью обсчитан
    updStat();
  }
}

function render() {
  const list = document.getElementById('list');
  rowsByName.clear();

  const cr = [`<span data-gopath="/">${escAttr(cardName())}</span>`];
  let acc = '';
  for (const p of curPath.split('/').filter(Boolean)) {
    acc += '/' + p;
    cr.push(`<span class="sep">›</span><span data-gopath="${escAttr(acc)}">${escAttr(p)}</span>`);
  }
  document.getElementById('crumbs').innerHTML = cr.join('');

  if (!curItems.length) {
    list.innerHTML = '<tr><td colspan="4" class="dim">Папка пуста</td></tr>';
    updStat();
    return;
  }

  list.innerHTML = curItems.map((k, i) => k.dir
    ? `<tr class="dir" data-i="${i}"><td>📁 <span class="nm" data-act="open" data-gopath="${escAttr(k.path)}">${escAttr(k.name)}/</span></td>
         <td class="hide-m dim">папка</td>
         <td class="hide-m"><span class="tag" data-role="sub">${k.childCount == null ? '…' : k.childCount + ' об.'}</span></td>
         <td><button class="sec" style="padding:4px 10px" data-act="del" title="Удалить пустую папку">🗑</button></td></tr>`
    : `<tr data-i="${i}"><td>📄 <span class="name">${escAttr(k.name)}</span></td>
         <td class="hide-m dim" data-role="size">${fmt(k.size)}</td>
         <td class="hide-m"><button class="sec" style="padding:4px 10px" data-act="info">ⓘ</button></td>
         <td><button class="sec" style="padding:4px 10px" data-act="dl">⬇ Скач.</button> <button class="sec" style="padding:4px 10px" data-act="del" title="Удалить">🗑</button></td></tr>`
  ).join('');

  [...list.querySelectorAll('tr')].forEach(tr => {
    const k = curItems[tr.dataset.i];
    if (!k) return;
    rowsByName.set(k.name, {
      dir: k.dir,
      sizeEl: tr.querySelector('[data-role="size"]'),
      subEl: tr.querySelector('[data-role="sub"]'),
      item: k
    });
  });
  updStat();
}

function updStat() {
  let files = 0, bytes = 0, dirs = 0, pending = 0;
  for (const k of curItems) {
    if (k.dir) { dirs++; if (k.childCount == null) pending++; }
    else if (k.size == null) pending++;
    else { files++; bytes += k.size; }
  }
  document.getElementById('stat').textContent =
    `${files} файл. (${fmt(bytes)}) · ${dirs} пап.` + (pending ? ` · размеры ${pending}…` : '');
}

function go(p) {
  if (p === '/' && rootData) {
    curPath = '/';
    curItems = rootData;
    render();
    if (!rootComplete) fillSizes(++loadToken, '/');   // домергиваить оставшиеся размеры
    return;
  }
  loadDir(p);
}

function changeCard(v) {
  card = parseInt(v);
  rootData = null; rootComplete = false;
  go('/');
}

function refresh() { loadDir(curPath || '/'); }

function setBar(pct, text) {
  document.getElementById('bar').firstChild.style.width = (pct ? pct : 0) + '%';
  document.getElementById('btxt').textContent = text || '';
}

function dl(path) {
  const it = curItems.find(k => k.path === path);
  const size = (it && typeof it.size === 'number') ? it.size : 0;
  const t0 = Date.now();
  const xhr = new XMLHttpRequest();
  xhr.open('GET', `/download?card=${card}&path=${encodeURIComponent(path)}`);
  xhr.responseType = 'blob';
  xhr.onprogress = ev => {
    const pct = size ? (ev.loaded / size * 100).toFixed(0) : '';
    const t = (Date.now() - t0) / 1000;
    setBar(size ? ev.loaded / size * 100 : 100,
      (size ? pct + '% · ' : '') + fmt(ev.loaded) + ' · ' + fmt(t > 0 ? ev.loaded / t : 0) + '/с');
  };
  xhr.onload = xhr.onerror = () => {
    if (xhr.status === 200 && xhr.response && xhr.response.size > 0) {
      const a = document.createElement('a');
      a.href = URL.createObjectURL(xhr.response);
      a.download = path.split('/').pop();
      a.click();
      URL.revokeObjectURL(a.href);
    } else {
      alert('Ошибка: HTTP ' + xhr.status);
    }
    setBar(0, '');
  };
  xhr.send();
}

async function info(path) {
  try {
    const d = await getJson(`/api/file/info?card=${card}&path=${encodeURIComponent(path)}`);
    alert(`Карта: ${cardName()}\nПуть: ${d.path}\nИмя: ${d.name}\nТип: ${d.dir ? 'папка' : 'файл'}` +
      (d.dir ? `\nОбъектов: ${d.children}` : `\nРазмер: ${fmt(d.size)} (${d.size} Б)`) +
      `\n\nSD: всего ${fmt(d.totalBytes)} · занято ${fmt(d.usedBytes)} · свободно ${fmt(d.freeBytes)}`);
  } catch (e) { alert('Ошибка: ' + e.message); }
}

async function del(path) {
  const it = curItems.find(k => k.path === path);
  const isDir = it && it.dir;
  if (!confirm((isDir ? 'Удалить пустую папку ' : 'Удалить файл ') + path + '?')) return;
  try {
    await getJson(`/api/delete?card=${card}&path=${encodeURIComponent(path)}`);
    refresh();
  } catch (e) { alert('Удаление не удалось: ' + e.message); }
}

// ---------- делегирование кликов (без inline-обработчиков: имена с кавычками не ломают разметку) ----------
document.getElementById('crumbs').addEventListener('click', ev => {
  const t = ev.target.closest('[data-gopath]');
  if (t) go(t.dataset.gopath);
});

document.getElementById('list').addEventListener('click', ev => {
  const open = ev.target.closest('[data-act="open"]');
  if (open) { go(open.dataset.gopath); return; }
  const act = ev.target.closest('[data-act]');
  if (!act) return;
  const tr = act.closest('tr');
  const item = curItems[tr ? tr.dataset.i : -1];
  if (!item) return;
  if (act.dataset.act === 'info') info(item.path);
  else if (act.dataset.act === 'dl') dl(item.path);
  else if (act.dataset.act === 'del') del(item.path);
});

document.getElementById('upIn').addEventListener('change', e => {
  const files = [...e.target.files];
  if (!files.length) return;
  let i = 0;
  const next = () => {
    if (i >= files.length) {
      setBar(0, '');
      e.target.value = '';
      refresh();
      return;
    }
    const f = files[i++];
    const fd = new FormData();
    fd.append('file', f, f.name);
    const xhr = new XMLHttpRequest();
    xhr.open('POST', `/upload?card=${card}`);
    const t0 = Date.now();
    xhr.upload.onprogress = ev => {
      const t = (Date.now() - t0) / 1000;
      setBar(ev.loaded / ev.total * 100, (ev.loaded / ev.total * 100).toFixed(0) + '% · ' +
        fmt(t > 0 ? ev.loaded / t : 0) + '/с');
    };
    xhr.onload = xhr.onerror = next;
    xhr.send(fd);
  };
  next();
});

loadCards().then(() => loadDir('/'));
</script>
</body>
</html>)html";
