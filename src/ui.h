#pragma once

// Дизайн в стиле «тёмный минимал» (как референс): sticky-топбар со счётчиком,
// аккордеон информации о карте, список файлов без лишних рамок,
// нижняя панель-«композер» (карта / загрузка / статус / круглая кнопка).
// Вся динамика (прогрессивные размеры, скачивание, загрузка, удаление) прежняя.

static const char INDEX_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Files</title>
<style>
  :root {
    --bg:#0b0c0e; --panel:#131519; --panel2:#181b20; --line:#23262d;
    --txt:#d8dbe2; --dim:#8a90a0; --acc:#5b9dff; --gold:#e5c07b;
    --ok:#7ee08a; --bad:#ff6b6b;
  }
  * { box-sizing:border-box; margin:0; padding:0; }
  body { font-family:'Segoe UI',system-ui,sans-serif; background:var(--bg); color:var(--txt);
         padding-bottom:96px; }
  .mono { font-family:ui-monospace,'Cascadia Mono',Consolas,monospace; }
  .dim { color:var(--dim); }
  .bad { color:var(--bad); }

  /* ---------- верхняя панель ---------- */
  #top { position:sticky; top:0; z-index:5; display:flex; align-items:center; gap:10px;
         padding:10px 16px; background:rgba(14,16,19,.94); border-bottom:1px solid var(--line);
         backdrop-filter:blur(6px); }
  .ttl { font-weight:700; font-size:.95rem; white-space:nowrap; }
  #crumbs { flex:1 1 auto; min-width:0; font-size:.82rem; overflow:hidden;
            text-overflow:ellipsis; white-space:nowrap; }
  #crumbs span { cursor:pointer; color:var(--acc); }
  #crumbs .sep { cursor:default; color:var(--dim); margin:0 4px; }
  #statTop { margin-left:auto; color:var(--dim); font-size:.78rem; white-space:nowrap; }
  .ic { width:34px; height:34px; border-radius:9px; background:var(--panel2);
        border:1px solid var(--line); color:var(--txt); cursor:pointer; font-size:1rem; }
  .ic:hover { background:#1f232b; }

  /* ---------- аккордеон карты ---------- */
  #cardBox { max-width:980px; margin:10px auto 0; background:var(--panel);
             border:1px solid var(--line); border-radius:10px; font-size:.84rem; }
  #cardBox summary { padding:9px 14px; cursor:pointer; color:var(--dim); list-style:none; }
  #cardBox summary::-webkit-details-marker { display:none; }
  #cardBox summary::before { content:'⌄ '; color:var(--dim); }
  #cardBox[open] summary::before { content:'⌃ '; }
  #cardBox summary:hover { color:var(--txt); }
  #cardDetails { padding:0 14px 10px; color:var(--dim); line-height:1.65; }

  /* ---------- панель навигации (назад/вперёд) ---------- */
  #navBar { max-width:980px; margin:10px auto 0; display:flex; gap:8px; align-items:center;
            background:var(--panel); border:1px solid var(--line); border-radius:10px; padding:6px; }
  .navB { width:44px; height:34px; font-size:1.05rem; }
  button:disabled { opacity:.35; cursor:default; }
  button:disabled:hover { background:var(--panel2); }

  /* ---------- список файлов ---------- */
  main { max-width:980px; margin:10px auto; background:var(--panel);
         border:1px solid var(--line); border-radius:10px; overflow:hidden; }
  table { width:100%; border-collapse:collapse; font-size:.88rem; }
  th { display:none; }
  td { padding:9px 12px; border-bottom:1px solid #171a1f; }
  tr:last-child td { border-bottom:none; }
  tr:hover td { background:#15181d; }
  .name { color:var(--acc); cursor:pointer; word-break:break-all; }
  .dir .nm { color:var(--gold); font-weight:600; }
  td.meta { color:var(--dim); font-size:.78rem; white-space:nowrap; }
  td.act { text-align:right; white-space:nowrap; }   /* кнопки всегда к правому краю — колонка ровная */
  .tag { display:inline-block; background:var(--panel2); color:var(--dim);
         border-radius:6px; padding:2px 7px; font-size:.72rem; }
  button { cursor:pointer; border:none; }
  .dlB { background:var(--panel2); color:var(--txt); border-radius:8px; padding:6px 11px;
         font-size:.78rem; border:1px solid var(--line); vertical-align:middle; }
  .dlB:hover { background:#1f232b; }
  .icB { width:30px; height:30px; border-radius:8px; background:var(--panel2); color:var(--txt);
         border:1px solid var(--line); font-size:.85rem; vertical-align:middle; }
  .icB:hover { background:#2a2320; }   /* лёгкий тёплый ховер на удалении */
  .rowEmpty { padding:18px 12px; color:var(--dim); text-align:center; }

  /* ---------- нижняя панель-композер ---------- */
  #barholder { position:fixed; left:0; right:0; bottom:0; z-index:6;
               background:rgba(14,16,19,.96); border-top:1px solid var(--line);
               backdrop-filter:blur(6px); }
  #bar { position:absolute; top:-2px; left:0; right:0; height:3px; }
  #bar div { height:100%; width:0; background:var(--acc); transition:width .2s; }
  .bar-in { max-width:980px; margin:0 auto; display:flex; gap:10px; align-items:center;
            padding:12px 16px; }
  #cardSel { background:var(--panel2); color:var(--txt); border:1px solid var(--line);
             border-radius:20px; padding:9px 14px; font-size:.85rem; cursor:pointer; outline:none; }
  .pill { background:var(--panel2); color:var(--txt); border:1px solid var(--line);
          border-radius:20px; padding:9px 14px; font-size:.85rem; cursor:pointer; }
  .pill:hover { background:#1f232b; }
  #upIn { display:none; }
  #btxt { color:var(--acc); font-size:.78rem; }
  #stat { margin-left:auto; color:var(--dim); font-size:.78rem; text-align:right; }
  .circ { width:38px; height:38px; border-radius:50%; background:var(--acc); color:#fff;
          font-size:1.1rem; box-shadow:0 2px 8px rgba(0,0,0,.4); }
  .circ:hover { filter:brightness(1.1); }

  .pill, #cardSel, .dlB { white-space:nowrap; flex-shrink:0; }
  #stat { margin-left:auto; min-width:0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
  #btxt { white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }

  @media(max-width:640px){
    #top { flex-wrap:wrap; }
    #upWrap   { order:3; margin-left:auto; }
    #top .ic  { order:4; }
    #crumbs   { order:5; flex-basis:100%; }   /* путь — отдельной строкой, не пропадает */
    #statTop { display:none; }
    .hide-m { display:none; }
    .pill, #cardSel { padding:8px 10px; font-size:.78rem; }
  }
</style>
</head>
<body>

<header id="top">
  <div class="logo">💾</div>
  <div class="ttl">ESP32 Files</div>
  <div id="crumbs"></div>
  <span id="statTop" class="mono"></span>
  <label class="pill" id="upWrap" style="position:relative">⬆ Загрузить
    <input id="upIn" type="file" multiple>
  </label>
  <button class="ic" onclick="refresh()" title="Обновить">⟳</button>
</header>

<details id="cardBox">
  <summary id="cardSummary">Карта…</summary>
  <div id="cardDetails" class="mono"></div>
</details>

<div id="navBar">
  <button class="ic navB" id="btnBack" title="Назад (Alt+←)" onclick="hGo(-1)" disabled>←</button>
  <button class="ic navB" id="btnFwd" title="Вперёд (Alt+→)" onclick="hGo(1)" disabled>→</button>
</div>

<main>
  <table>
    <thead><tr><th>Путь</th><th>Размер</th><th>Инфо</th><th></th></tr></thead>
    <tbody id="list"><tr><td colspan="4" class="rowEmpty dim">Загрузка…</td></tr></tbody>
  </table>
</main>

<footer id="barholder">
  <div id="bar"><div></div></div>
  <div class="bar-in">
    <select id="cardSel" onchange="changeCard(this.value)"></select>
    <button class="ic" onclick="refresh()" title="Обновить">⟳</button>
    <span id="btxt" class="mono"></span>
    <span id="stat" class="mono"></span>
    <button class="circ" onclick="window.scrollTo({top:0,behavior:'smooth'})" title="Наверх">↑</button>
  </div>
</footer>

<script>
// Быстрая загрузка: /api/files отдаёт только имена и тип объектов (один readdir-проход),
// размеры доматываются плавно порциями через /api/sizes (см. README, раздел API).
let rootData = null;      // кэш содержимого корня
let rootComplete = false; // размеры корня уже целиком получены
let curPath = '/';
let curItems = [];
let card = 0;
let cardsData = [];
let loadToken = 0;        // инкрементируется при смене контекста — отменяет устаревшие подгрузки
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
    cardsData = d.cards || [];
    sel.innerHTML = cardsData.map(c =>
      `<option value="${c.index}" ${c.ready ? '' : 'disabled'}>` +
      `${escAttr(c.name)} ${c.ready ? '✓' : '(нет)'}</option>`).join('');
    if (sel.querySelector('option[value="0"]')) sel.value = 0;
  } catch (e) {
    cardsData = [];
    sel.innerHTML = '<option>—</option>';
  }
  renderCardBox();
}

function cardName() {
  const o = document.getElementById('cardSel').selectedOptions[0];
  return o ? o.textContent.replace(/[✓]$/,'').trim() : '?';
}

const typeStr = t => t === 3 ? 'SDHC' : t === 2 ? 'SD' : t === 1 ? 'MMC' : t === 0 ? 'нет' : '—';

function renderCardBox() {
  const c = cardsData.find(x => x.index === card) || cardsData[0];
  const el = document.getElementById('cardSummary');
  const det = document.getElementById('cardDetails');
  if (!c) { el.textContent = 'Карта: неизвестна'; det.textContent = ''; return; }
  if (!c.ready) { el.textContent = `💾 ${c.name} — не готова`; det.textContent = ''; return; }
  el.textContent = `💾 ${c.name} — занято ${fmt(c.usedBytes)} из ${fmt(c.totalBytes)} (${typeStr(c.type)})`;
  det.textContent = `всего ${fmt(c.totalBytes)} · занято ${fmt(c.usedBytes)} · свободно ${fmt(c.freeBytes)} · индекс ${c.index}`;
}

// ---------- история навигации (назад/вперёд, как в браузере) ----------
const hstack = [];
let hpos = -1;

function hUpdate() {
  document.getElementById('btnBack').disabled = hpos <= 0;
  document.getElementById('btnFwd').disabled = hpos >= hstack.length - 1;
}
function hPush(path) {
  if (hstack[hpos] === path) return;               // то же место — не дублируем
  hstack.splice(hpos + 1);                          // обрезаем устаревший «хвост вперёд»
  hstack.push(path);
  hpos = hstack.length - 1;
  hUpdate();
}
function hGo(step) {                               // -1 назад, +1 вперёд
  const target = hpos + step;
  if (target < 0 || target >= hstack.length) return;
  hpos = target;
  hUpdate();
  const to = hstack[target];
  if (to === '/' && rootData) {                     // корень отдаём из кэша, если он жив
    curPath = '/';
    curItems = rootData;
    render();
    if (!rootComplete) fillSizes(++loadToken, '/');
  } else {
    loadDir(to, false);
  }
}

document.addEventListener('keydown', ev => {
  if (!ev.altKey) return;
  if (ev.key === 'ArrowLeft') { ev.preventDefault(); hGo(-1); }
  else if (ev.key === 'ArrowRight') { ev.preventDefault(); hGo(1); }
});

async function loadDir(path, push = true) {
  const token = ++loadToken;
  const list = document.getElementById('list');
  list.innerHTML = '<tr><td colspan="4" class="rowEmpty dim">Загрузка…</td></tr>';
  document.getElementById('stat').textContent = '';
  document.getElementById('statTop').textContent = '';
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
    if (push) hPush(curPath);
    render();
    fillSizes(token, curPath);
  } catch (e) {
    if (token !== loadToken) return;
    list.innerHTML = '<tr><td colspan="4" class="rowEmpty bad">Ошибка: ' + e.message + '</td></tr>';
  }
}

// Плавная (постепенная) загрузка размеров: порциями по BATCH, каждый ответ
// раскрашивает нужные ячейки сразу.
async function fillSizes(token, dir, start = 0) {
  if (token !== loadToken) return;
  let d;
  try {
    d = await getJson(`/api/sizes?card=${card}&dir=${encodeURIComponent(dir || '/')}&start=${start}&count=${BATCH}`);
  } catch (e) {
    if (token === loadToken) document.getElementById('stat').textContent = 'размеры недоступны: ' + e.message;
    return;
  }
  if (token !== loadToken) return;

  for (const it of (d.items || [])) {
    const t = rowsByName.get(it.name);
    if (!t || !t.item) continue;                    // объект исчез/папка сменилась
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
    rootComplete = true;
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
    list.innerHTML = '<tr><td colspan="4" class="rowEmpty dim">Папка пуста</td></tr>';
    updStat();
    return;
  }

  list.innerHTML = curItems.map((k, i) => k.dir
    ? `<tr class="dir" data-i="${i}"><td>📁 <span class="nm" data-act="open" data-gopath="${escAttr(k.path)}">${escAttr(k.name)}/</span></td>
         <td class="hide-m meta">папка</td>
         <td class="hide-m meta"><span class="tag" data-role="sub">${k.childCount == null ? '…' : k.childCount + ' об.'}</span></td>
         <td class="act"><button class="icB" data-act="del" title="Удалить пустую папку">🗑</button></td></tr>`
    : `<tr data-i="${i}"><td>📄 <span class="name">${escAttr(k.name)}</span></td>
         <td class="hide-m meta" data-role="size">${fmt(k.size)}</td>
         <td class="hide-m meta"><button class="icB" data-act="info" title="Инфо">ⓘ</button></td>
         <td class="act"><button class="dlB" data-act="dl">⬇ Скач.</button> <button class="icB" data-act="del" title="Удалить">🗑</button></td></tr>`
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
  const line = `${files} файл. (${fmt(bytes)}) · ${dirs} пап.` + (pending ? ` · размеры ${pending}…` : '');
  document.getElementById('stat').textContent = line;
  document.getElementById('statTop').textContent = line;
}

function go(p) {
  if (p === '/' && rootData) {
    curPath = '/';
    curItems = rootData;
    render();
    hPush('/');                                       // пришли сюда — запишем в историю
    if (!rootComplete) fillSizes(++loadToken, '/');   // доматывать оставшиеся размеры
    return;
  }
  loadDir(p, true);
}

function changeCard(v) {
  card = parseInt(v);
  rootData = null; rootComplete = false;
  hstack.length = 0; hpos = -1;      // у каждой карты своя история
  renderCardBox();
  go('/');
}

function refresh() { loadDir(curPath || '/', false); }   // перезагрузка не путает историю

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
