// ---------- presets ----------
function soundSnap() { const x = snapshot(); return { knobs: x.knobs, parts: x.parts, ...(x.tune && { tune: x.tune }), wave: x.wave, fx: x.fx }; }
function patternSnap() { return { bpm: state.bpm, shuffle: state.shuffle, gate: state.gate, pattern: state.pattern.map(st => ({ ...st })) }; }
function applySound(x) {
  const c = cleanSettings(x);
  for (const k of KNOBS) state.knobs[k.id] = c.knobs[k.id] ?? k.def;
  for (const k in COMPONENTS) state.parts[k] = c.parts[k] ?? COMPONENTS[k].v;
  state.wave = c.wave || 'saw';
  state.tune = c.tune || new Array(12).fill(0); sendTune();
  state.fx = c.fx || { rat: false, lex: false, dly: false, fold: false };
  for (const n in state.fx) { preview.setFx(n, state.fx[n]); send({ type: 'fx', name: n, value: state.fx[n] }); }
  renderFx();
  for (const k of KNOBS) { preview.setKnob(k.id, state.knobs[k.id]); send({ type: 'knob', name: k.id, value: state.knobs[k.id] }); }
  for (const k in COMPONENTS) { preview.setComponent(k, state.parts[k]); send({ type: 'component', name: k, value: state.parts[k] }); }
  preview.waveform = state.wave; send({ type: 'wave', value: state.wave });
  drawWave();
  renderParts(); renderFrontMods(); drawTrim(); drawKeyTrack(); drawTune(); for (const id in knobEls) drawKnob(id); readouts(); saveSession();
}
function applyPattern(x) {
  const c = cleanSettings(x);
  if (c.bpm) { state.bpm = c.bpm; drawBpm(); send({ type: 'bpm', value: state.bpm }); }
  if (c.shuffle != null) state.shuffle = c.shuffle; if (c.gate != null) state.gate = c.gate;
  if (c.pattern) { state.pattern = c.pattern; tail = []; }
  renderSeq(); pushPattern();
}
function loadStore(key) { const v = store.get(key, {}); return v && typeof v === 'object' && !Array.isArray(v) ? v : {}; }
// one-time migration from the single combined preset list
{
  const old = store.get('presets', null);
  if (old && typeof old === 'object' && !store.get('migrated2', false)) {
    const sp = loadStore('soundPresets'), pp = loadStore('patternPresets');
    for (const [n, x] of Object.entries(old)) { const c = cleanSettings(x); sp[n] ||= { knobs: c.knobs, parts: c.parts, wave: c.wave }; if (c.pattern) pp[n] ||= { bpm: c.bpm, pattern: c.pattern }; }
    store.set('soundPresets', sp); store.set('patternPresets', pp); store.set('migrated2', true);
  }
}
function presetBar(pre, storeKey, builtins, snap, apply, autoName) {
  const sel = document.getElementById(pre + '-preset'), nameEl = document.getElementById(pre + '-name');
  const delBtn = document.getElementById(pre + '-del'), msg = document.getElementById(pre + '-msg');
  const bar = { user: loadStore(storeKey), say: (t) => { msg.textContent = t; clearTimeout(bar.t); bar.t = setTimeout(() => { msg.textContent = ''; }, 4000); } };
  let armed = '';
  const esc = (n) => n.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;');
  bar.render = (selected) => {
    const opt = (g, n) => `<option value="${g}:${esc(n)}">${esc(n.split(' / ').pop())}</option>`;
    const names = Object.keys(bar.user).sort((a, b) => a.localeCompare(b));
    const groups = {};
    for (const n of Object.keys(builtins)) (groups[n.includes(' / ') ? n.split(' / ')[0] : 'Factory'] ||= []).push(n);
    sel.innerHTML = '<option value="">Choose…</option>'
      + Object.entries(groups).map(([g, ns]) => `<optgroup label="${esc(g)}">` + ns.map(n => opt('b', n)).join('') + '</optgroup>').join('')
      + (names.length ? '<optgroup label="Saved">' + names.map(n => opt('u', n)).join('') + '</optgroup>' : '');
    sel.value = selected || '';
    delBtn.disabled = !sel.value.startsWith('u:'); armed = ''; delBtn.textContent = 'Delete';
    sel.dispatchEvent(new Event('sync'));
  };
  bar.save = () => store.set(storeKey, bar.user);
  // auto-namer: fill the name box with a suggestion, unless the person typed their own
  let suggested = '';
  bar.suggest = (force) => {
    if (!autoName || (!force && nameEl.value.trim() && nameEl.value !== suggested)) return;
    nameEl.value = suggested = autoName(bar.user);
  };
  sel.addEventListener('change', () => {
    const v = sel.value; delBtn.disabled = !v.startsWith('u:'); armed = ''; delBtn.textContent = 'Delete';
    if (!v) return;
    const name = v.slice(2);
    apply(v[0] === 'b' ? builtins[name] : bar.user[name]);
    if (v[0] === 'u') nameEl.value = name;
    bar.say(`Loaded ${name}.`);
  });
  document.getElementById(pre + '-save').onclick = () => {
    if (autoName && !nameEl.value.trim()) bar.suggest(true);
    const name = nameEl.value.trim();
    if (!name) { nameEl.focus(); bar.say('Type a name first, then save.'); return; }
    suggested = '';
    const existed = name in bar.user;
    bar.user[name] = snap(); bar.save(); bar.render('u:' + name);
    bar.say(existed ? `Updated ${name}.` : `Saved ${name}.`);
  };
  nameEl.addEventListener('keydown', (e) => { if (e.key === 'Enter') document.getElementById(pre + '-save').click(); });
  delBtn.onclick = () => {
    const v = sel.value; if (!v.startsWith('u:')) return;
    const name = v.slice(2);
    if (armed !== name) { armed = name; delBtn.textContent = 'Confirm delete'; bar.say(`Click Confirm delete to remove ${name}.`); return; }
    delete bar.user[name]; bar.save(); bar.render(''); bar.say(`Deleted ${name}.`);
  };
  bar.render('');
  return bar;
}
// ---------- preset menu: our own picker over the hidden <select>, grouped, searchable, with prev/next ----------
function presetMenu(host) {
  const sel = document.getElementById(host.dataset.for), pre = host.dataset.for.replace('-preset', '');
  const isPat = pre === 'pat';
  host.innerHTML = `<button type="button" class="pm-btn" aria-haspopup="listbox" aria-expanded="false"><span class="cur"></span></button>`
    + `<button type="button" class="pm-step" data-d="-1" aria-label="Previous preset">◀</button><button type="button" class="pm-step" data-d="1" aria-label="Next preset">▶</button>`
    + `<div class="pm-pop" hidden><div class="pm-head"><div class="pm-qrow"><input type="text" class="pm-q" placeholder="Search" aria-label="Search presets"><button type="button" class="pm-favf" aria-pressed="false" title="Show favourites only">★</button></div>`
    + (isPat ? `<div class="pm-chips" role="group" aria-label="Filter">${PM_TAGS.map(t => `<button type="button" class="pm-chip" data-t="${t.k}" aria-pressed="false" title="${t.tip}">${t.label}</button>`).join('')}</div>` : '')
    + `</div><div class="pm-list" role="listbox" aria-label="${sel.getAttribute('aria-label')}"></div></div>`;
  host.querySelector('.pm-pop').appendChild(host.parentElement.querySelector('.pm-foot'));
  const btn = host.querySelector('.pm-btn'), pop = host.querySelector('.pm-pop'), q = host.querySelector('.pm-q'), list = host.querySelector('.pm-list');
  const esc = (t) => String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
  const coll = new Intl.Collator(undefined, { numeric: true, sensitivity: 'base' });
  const favKey = 'fav.' + pre, recKey = 'recent.' + pre, openKey = 'pmOpen.' + pre;
  const fav = new Set(store.get(favKey, [])), open = new Set(store.get(openKey, ['fav', 'rec', 'my', 'built']));
  let recent = store.get(recKey, []), chips = new Set(), act = -1, tree = null, order = [], byV = new Map(), favOnly = !!store.get('favOnly.' + pre, false);

  // every option becomes an item with a folder path: My presets, Built-in, or source > author > song
  function build() {
    const items = [...sel.querySelectorAll('option')].filter(o => o.value).map(o => {
      const v = o.value, full = v.slice(2);
      let path, name = full;
      if (v.startsWith('u:')) path = ['my'];
      else if (full.includes(' / ')) {
        const [grp, nm] = full.split(' / '); name = nm;
        const m = /^ABL3 (.+)$/.exec(grp); path = m ? ['ABL3', m[1]] : [grp];
      } else path = ['built'];
      name = name.replace(/_/g, ' ').replace(/\s+/g, ' ').trim();
      return { v, name, path, label: name };
    });
    byV = new Map(items.map(it => [it.v, it]));
    const root = { kids: new Map(), items: [] };
    const node = (path) => { let n = root; for (const k of path) { if (!n.kids.has(k)) n.kids.set(k, { key: (n.key ? n.key + '/' : '') + k, label: k, kids: new Map(), items: [] }); n = n.kids.get(k); } return n; };
    for (const it of items) node(it.path).items.push(it);
    // multi-part songs ("Name - Part") become one row inside their folder
    const songs = (n) => {
      const stems = new Map();
      for (const it of n.items) { const i = it.name.indexOf(' - '); if (i > 0) { const st = it.name.slice(0, i); (stems.get(st) || stems.set(st, []).get(st)).push(it); } }
      for (const [st, its] of stems) if (its.length > 1) {
        const k = { key: n.key + '/' + st, label: st, kids: new Map(), items: its, song: true };
        for (const it of its) it.label = it.name.slice(st.length + 3);
        n.items = n.items.filter(it => !its.includes(it)); n.kids.set(st, k);
      }
      for (const c of n.kids.values()) if (!c.song) songs(c);
    };
    for (const c of root.kids.values()) if (c.key !== 'my' && c.key !== 'built') songs(c);
    const LABEL = { my: 'My presets', built: 'Factory' };
    for (const [k, c] of root.kids) if (LABEL[k]) c.label = LABEL[k];
    const sortN = (n) => { n.items.sort((a, b) => coll.compare(a.label, b.label)); for (const c of n.kids.values()) sortN(c); };
    sortN(root);
    // top level order: mine, built-in, then sources A-Z
    const top = [...root.kids.values()].sort((a, b) => (a.key === 'my' ? -2 : a.key === 'built' ? -1 : 0) - (b.key === 'my' ? -2 : b.key === 'built' ? -1 : 0) || coll.compare(a.label, b.label));
    tree = top; order = [];
    // folders before loose items inside a folder, song rows sorted in with folders
    const walk2 = (n) => { n.sorted = [...n.kids.values()].sort((a, b) => (a.song - b.song) || coll.compare(a.label, b.label)); for (const c of n.sorted) walk2(c); };
    for (const t of top) walk2(t);
    const flatten = (n) => { for (const c of n.sorted) if (!c.song) flatten(c); const rows = [...n.sorted.filter(c => c.song).map(c => ({ song: c })), ...n.items.map(it => ({ it }))].sort((a, b) => coll.compare((a.song || a.it).label, (b.song || b.it).label)); for (const r of rows) if (r.song) order.push(...r.song.items); else order.push(r.it); };
    for (const t of top) flatten(t);
    recent = recent.filter(v => byV.has(v)); for (const v of [...fav]) if (!byV.has(v)) fav.delete(v);
  }
  const small = () => byV.size <= 14;
  const pathText = (it) => it.path.map(k => ({ my: 'My presets', built: 'Factory' }[k] || k)).join(' › ');
  function sync() {
    const it = byV.get(sel.value);
    btn.querySelector('.cur').textContent = it ? it.name : 'Choose…';
  }
  const row = (it, d, hint) => `<div class="pm-row" style="--d:${d}"><button type="button" class="pm-i" role="option" data-v="${esc(it.v)}" aria-selected="${it.v === sel.value}">${esc(hint ? it.name : it.label)}${hint ? `<span class="pm-path">${esc(pathText(it))}</span>` : ''}</button>`
    + `<button type="button" class="pm-star" data-v="${esc(it.v)}" aria-pressed="${fav.has(it.v)}" aria-label="${fav.has(it.v) ? 'Remove from' : 'Add to'} favourites">★</button></div>`;
  const folder = (key, label, n, d) => `<button type="button" class="pm-g${d ? ' sub' : ''}" style="--d:${d}" data-g="${esc(key)}" aria-expanded="${open.has(key)}">${esc(label)}<span class="n">${n}</span></button>`;
  const count = (n) => n.items.length + [...n.kids.values()].reduce((a, c) => a + count(c), 0);
  function drawNode(n, d) {
    let h = folder(n.key, n.label, count(n), d);
    if (!open.has(n.key)) return h;
    const rows = [...n.sorted.map(c => ({ c })), ...n.items.map(it => ({ it }))];
    rows.sort((a, b) => (a.c && !a.c.song ? 0 : 1) - (b.c && !b.c.song ? 0 : 1) || coll.compare((a.c || a.it).label, (b.c || b.it).label));
    for (const r of rows) h += r.c ? drawNode(r.c, d + 1) : row(r.it, d + 1);
    return h;
  }
  function draw() {
    const term = q.value.trim().toLowerCase(); let h = '';
    if (term || chips.size || favOnly) {
      const hits = order.filter(it => (!favOnly || fav.has(it.v)) && (!term || (it.name + ' ' + pathText(it)).toLowerCase().includes(term)) && [...chips].every(t => pmTags(it.v).has(t)));
      h = hits.slice(0, 300).map(it => row(it, 0, byV.size > 14)).join('');
      h = (hits.length && (term || chips.size) ? `<div class="pm-sum">${hits.length} match${hits.length === 1 ? '' : 'es'}${hits.length > 300 ? ', first 300 shown' : ''}</div>` : '') + h;
    } else {
      if (small()) h = order.map(it => row(it, 0)).join('');
      else for (const t of tree) h += drawNode(t, 0);
    }
    list.innerHTML = h || `<div class="pm-none">${!byV.size ? 'No presets yet. Save one below.' : favOnly && !term ? 'No favourites yet. Tap ☆ on a preset to add it.' : 'Nothing matches.'}</div>`;
    act = -1;
  }
  function show() {
    build();
    const it = byV.get(sel.value);
    if (it) { let k = ''; for (const p of it.path) { k = k ? k + '/' + p : p; open.add(k); } const i = it.name.indexOf(' - '); if (i > 0) open.add(it.path.join('/') + '/' + it.name.slice(0, i)); }
    q.value = ''; draw(); pop.hidden = false; pop.style.maxHeight = Math.max(240, Math.min(520, innerHeight - btn.getBoundingClientRect().bottom - 16)) + 'px'; btn.setAttribute('aria-expanded', 'true');
    if (matchMedia('(pointer: fine)').matches) q.focus();
    list.querySelector('.pm-g ~ .pm-row [aria-selected=true], .pm-row [aria-selected=true]')?.scrollIntoView({ block: 'center' });
  }
  function hide(refocus) { pop.hidden = true; btn.setAttribute('aria-expanded', 'false'); if (refocus) btn.focus(); }
  function choose(v, remember) {
    sel.value = v; sel.dispatchEvent(new Event('change')); sync();
    if (remember) { recent = [v, ...recent.filter(x => x !== v)].slice(0, 8); store.set(recKey, recent); }
  }
  btn.onclick = () => (pop.hidden ? show() : hide());
  host.querySelectorAll('.pm-step').forEach(b => b.onclick = () => {
    build(); const L = favOnly && order.some(it => fav.has(it.v)) ? order.filter(it => fav.has(it.v)) : order; if (!L.length) return;
    const d = +b.dataset.d, i = L.findIndex(it => it.v === sel.value);
    choose(L[i < 0 ? (d > 0 ? 0 : L.length - 1) : (i + d + L.length) % L.length].v);
  });
  list.addEventListener('click', (e) => {
    const g = e.target.closest('.pm-g');
    if (g) { open.has(g.dataset.g) ? open.delete(g.dataset.g) : open.add(g.dataset.g); store.set(openKey, [...open]); const y = list.scrollTop; draw(); list.scrollTop = y; return; }
    const st = e.target.closest('.pm-star');
    if (st) { const v = st.dataset.v; fav.has(v) ? fav.delete(v) : fav.add(v); store.set(favKey, [...fav]); const y = list.scrollTop; draw(); list.scrollTop = y; return; }
    const it = e.target.closest('.pm-i'); if (it) { choose(it.dataset.v, true); hide(true); }
  });
  host.querySelector('.pm-chips')?.addEventListener('click', (e) => {
    const c = e.target.closest('.pm-chip'); if (!c) return;
    chips.has(c.dataset.t) ? chips.delete(c.dataset.t) : chips.add(c.dataset.t); c.setAttribute('aria-pressed', chips.has(c.dataset.t)); draw();
  });
  q.addEventListener('input', draw);
  const favBtn = host.querySelector('.pm-favf');
  favBtn.setAttribute('aria-pressed', favOnly);
  favBtn.onclick = () => { favOnly = !favOnly; favBtn.setAttribute('aria-pressed', favOnly); store.set('favOnly.' + pre, favOnly); draw(); };
  pop.addEventListener('keydown', (e) => {
    const items = [...list.querySelectorAll('.pm-i, .pm-g')];
    if (e.key === 'Escape') { e.preventDefault(); hide(true); return; }
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault(); if (!items.length) return;
      act = Math.max(0, Math.min(items.length - 1, act + (e.key === 'ArrowDown' ? 1 : -1)));
      items.forEach((el, i) => el.classList.toggle('act', i === act)); items[act].scrollIntoView({ block: 'nearest' });
    } else if (e.key === 'Enter' && act >= 0 && items[act]) { e.preventDefault(); items[act].click(); }
  });
  document.addEventListener('pointerdown', (e) => { if (!pop.hidden && !host.contains(e.target)) hide(); });
  sel.addEventListener('sync', () => { build(); sync(); if (!pop.hidden) { const y = list.scrollTop; draw(); list.scrollTop = y; } }); sel.addEventListener('change', sync);
  build(); sync();
}
// pattern tags, worked out from the notes themselves
const PM_TAGS = [
  { k: 'short', label: 'Short', tip: 'Under 16 steps' }, { k: 'long', label: 'Long', tip: 'Over 16 steps' },
  { k: 'sparse', label: 'Sparse', tip: 'A quarter or more of the steps are rests' }, { k: 'slidey', label: 'Slidey', tip: 'Slides on 40% or more of the steps' },
  { k: 'accents', label: 'Accents', tip: 'Accents on 40% or more of the steps' }, { k: 'tempo', label: 'Tempo', tip: 'Comes with its own tempo' },
];
const pmTagCache = new Map();
function pmTags(v) {
  let p;
  if (v.startsWith('b:')) p = PATTERN_BUILTIN[v.slice(2)]; else if (v.startsWith('u:')) p = loadStore('patternPresets')[v.slice(2)];
  if (v.startsWith('b:') && pmTagCache.has(v)) return pmTagCache.get(v);
  const t = new Set(), P = p && Array.isArray(p.pattern) ? p.pattern : [];
  if (P.length) {
    const n = P.length, frac = (f) => P.filter(f).length / n;
    if (n < 16) t.add('short'); if (n > 16) t.add('long');
    if (frac(s => !s.gate) >= 0.25) t.add('sparse');
    if (frac(s => s.slide) >= 0.4) t.add('slidey');
    if (frac(s => s.accent) >= 0.4) t.add('accents');
    if (p.bpm) t.add('tempo');
  }
  if (v.startsWith('b:')) pmTagCache.set(v, t);
  return t;
}
