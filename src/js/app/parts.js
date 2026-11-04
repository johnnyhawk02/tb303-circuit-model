// ---------- parts list ----------
const partsEl = document.getElementById('parts');
function partRange(k) {
  const d = COMPONENTS[k];
  if (d.range) return d.range;
  return ['Ω', 'F', 'A'].includes(d.unit) ? [d.v / 10, d.v * 10, 'log'] : [d.v * 0.5, d.v * 1.5, 'lin'];
}
const SL = 1000;
function toSlider(k, v) {
  const [lo, hi, m] = partRange(k);
  const t = m === 'log' ? Math.log(v / lo) / Math.log(hi / lo) : (v - lo) / (hi - lo);
  return Math.round(Math.min(1, Math.max(0, t)) * SL);
}
function fromSlider(k, x) {
  const [lo, hi, m] = partRange(k), t = x / SL;
  const v = m === 'log' ? lo * Math.pow(hi / lo, t) : lo + (hi - lo) * t;
  return +v.toPrecision(3);
}
const SRC_TEXT = { schematic: 'from the 1982 sheet', td3: 'read from the TD-3 sheet', rat: 'standard RAT value', lex: 'from Lexicon documentation', estimate: 'estimate' };
function partRow(k) {
  const d = COMPONENTS[k], v = state.parts[k];
  const changed = partChanged(k, v) ? ' changed' : '';
  const t = d.fixed ? 0.5 : toSlider(k, v) / SL;
  return `<div class="pk${d.mod ? ' mod' : ''}${d.fixed ? ' fixed' : ''}" data-k="${k}">
    <div class="knob" data-k="${k}" ${d.fixed ? '' : 'tabindex="0" role="slider"'} aria-label="${k}" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${Math.round(t * 100)}">${knobSVG()}</div>
    <span class="ref">${k}</span>
    <span class="val${changed}" data-k="${k}" title="${d.fixed ? 'Sets taper only' : 'Click to reset to ' + fmt(d.v, d.unit)}">${fmt(v, d.unit)}</span>
  </div>`;
}
function drawPartKnob(k) {
  const t = COMPONENTS[k].fixed ? 0.5 : toSlider(k, state.parts[k]) / SL;
  for (const el of document.querySelectorAll(`.knob[data-k="${k}"]`)) {
    knobDraw(el, t);
    el.setAttribute('aria-valuenow', Math.round(t * 100));
  }
}
const detailEl = document.getElementById('pdetail');
function showDetail(k) {
  const d = COMPONENTS[k];
  const esc = (x) => String(x).replace(/&/g, '&amp;').replace(/</g, '&lt;');
  detailEl.innerHTML = `<b>${k}</b> · ${esc(d.note)} (${SRC_TEXT[d.source] || d.source}). Stock ${fmt(d.v, d.unit)}${tolerance(k) ? `, real units ±${+(tolerance(k) * 100).toFixed(1)}%` : ''}${realSeries(k) ? `, moves in ${realSeries(k) === E24 ? 'E24' : 'E12'} steps (Shift for in-between)` : ''}.${d.mod ? `<br><span class="dot"></span>${esc(d.mod)}` : ''}`;
}
const BLOCKS = [...new Set(Object.values(COMPONENTS).map(d => d.block))];
const TABS = ['Mods', ...BLOCKS];
let partsTab = store.get('partsTab', 'Mods'); if (!TABS.includes(partsTab)) partsTab = 'Mods';
const tabsEl = document.getElementById('ptabs');
function tabKeys(t) { return Object.keys(COMPONENTS).filter(k => t === 'Mods' ? COMPONENTS[k].mod : COMPONENTS[k].block === t); }
function changedIn(t) { return tabKeys(t).filter(k => partChanged(k)).length; }
function renderTabs() {
  tabsEl.innerHTML = TABS.map(t => {
    const n = changedIn(t);
    return `<button type="button" role="tab" id="tab-${t}" data-t="${t}" aria-selected="${t === partsTab}" tabindex="${t === partsTab ? 0 : -1}" aria-controls="parts">${t}${n ? `<span class="n" title="${n} changed">●${n}</span>` : ''}</button>`;
  }).join('');
  partsEl.setAttribute('aria-labelledby', 'tab-' + partsTab);
}
function renderParts() {
  renderTabs();
  partsEl.classList.toggle('all-mod', partsTab === 'Mods');
  partsEl.innerHTML = tabKeys(partsTab).map(partRow).join('');
  for (const k of tabKeys(partsTab)) drawPartKnob(k);
}
function selectTab(t, focus) {
  partsTab = t; store.set('partsTab', t); renderParts();
  if (focus) document.getElementById('tab-' + t)?.focus();
}
tabsEl.addEventListener('click', (e) => { const b = e.target.closest('[role=tab]'); if (b) selectTab(b.dataset.t, true); });
tabsEl.addEventListener('keydown', (e) => {
  const i = TABS.indexOf(partsTab);
  const j = { ArrowRight: i + 1, ArrowLeft: i - 1, Home: 0, End: TABS.length - 1 }[e.key];
  if (j === undefined) return; e.preventDefault();
  selectTab(TABS[(j + TABS.length) % TABS.length], true);
});
function applyPart(k, val) {
  state.parts[k] = val;
  if (k === 'I_REF') queueMicrotask(drawTrim);
  if (k === 'KT' || k === 'R_KT') queueMicrotask(drawKeyTrack);
  for (const lab of document.querySelectorAll(`.val[data-k="${k}"]`)) { lab.textContent = fmt(val, COMPONENTS[k].unit); lab.classList.toggle('changed', partChanged(k, val)); }
  drawPartKnob(k);
  preview.setComponent(k, val); send({ type: 'component', name: k, value: val });
  for (const id in knobEls) drawKnob(id); readouts(); saveSession(); renderTabsSoon(); renderFx();
}
let tabTimer = 0;
function renderTabsSoon() { clearTimeout(tabTimer); tabTimer = setTimeout(renderTabs, 120); }
const partT = (k) => toSlider(k, state.parts[k]) / SL;
// real parts: resistors move in E24 steps and capacitors in E12, the values you can buy; Shift gives in-between values
const E24 = [1.0,1.1,1.2,1.3,1.5,1.6,1.8,2.0,2.2,2.4,2.7,3.0,3.3,3.6,3.9,4.3,4.7,5.1,5.6,6.2,6.8,7.5,8.2,9.1];
const E12 = [1.0,1.2,1.5,1.8,2.2,2.7,3.3,3.9,4.7,5.6,6.8,8.2];
const realSeries = (k) => { const d = COMPONENTS[k]; return ['schematic', 'td3', 'rat'].includes(d.source) && !d.fixed ? (d.unit === 'Ω' ? E24 : d.unit === 'F' ? E12 : null) : null; };
function seriesValues(k) {
  const E = realSeries(k), [lo, hi] = partRange(k), out = [];
  for (let dec = Math.floor(Math.log10(lo)) - 1; dec <= Math.ceil(Math.log10(hi)); dec++)
    for (const m of E) { const v = +(m * Math.pow(10, dec)).toPrecision(3); if (v >= lo * 0.999 && v <= hi * 1.001) out.push(v); }
  return out;
}
function snapReal(k, v) {
  if (!realSeries(k)) return v;
  let best = v, bd = Infinity;
  for (const x of seriesValues(k)) { const dd = Math.abs(Math.log(x / v)); if (dd < bd) { bd = dd; best = x; } }
  return best;
}
// one series step up or down from the current value (arrow keys and the wheel)
function stepReal(k, dir) {
  const cur = state.parts[k], vs = seriesValues(k);
  const next = dir > 0 ? vs.find(x => x > cur * 1.001) : [...vs].reverse().find(x => x < cur * 0.999);
  if (next) applyPart(k, next);
}
function setPartT(k, t, free) { t = Math.min(1, Math.max(0, t)); const v = fromSlider(k, t * SL); applyPart(k, free ? v : snapReal(k, v)); }
// ---------- front-page mods ----------
const FRONT_MODS = [
  ['R_SOFT', 'Soft attack', 'fade-in on normal notes'],
  ['C35', 'Slide time', 'how long glides take'],
  ['R138', 'Short decay', 'fastest and accent decay'],
  ['R52', 'Note length', 'how long the VCA rings'],
  ['C_ACC', 'Accent sweep', 'how accents build up'],
  ['R72', 'Env depth', 'size of the filter sweep'],
  ['V_CUT', 'Cutoff range', 'how far the cutoff reaches'],
  ['A_Q21', 'Reso limit', 'up to self-oscillation'],
  ['R_IN', 'Drive', 'push the filter harder'],
  ['C18', 'Ladder cap', 'resonance character'],
  ['WOW', 'Accent wow', 'swell on accents at high reso'],
  ['NOISE', 'Noise', 'hiss of the old parts'],
].filter(([k]) => k in COMPONENTS);
const modsEl = document.getElementById('mods');
function renderFrontMods() {
  modsEl.innerHTML = FRONT_MODS.map(([k, name, desc]) => {
    const d = COMPONENTS[k], v = state.parts[k];
    const changed = partChanged(k, v) ? ' changed' : '';
    return `<div class="md" data-k="${k}" title="${k}: ${d.mod.replace(/"/g, '&quot;')}">
      <div class="knob" data-k="${k}" tabindex="0" role="slider" aria-label="${name} (${k})" aria-valuemin="0" aria-valuemax="100">${knobSVG()}</div>
      <span class="mname">${name}</span><span class="mdesc">${desc}</span>
      <span class="val${changed}" data-k="${k}" title="Click to reset to ${fmt(d.v, d.unit)}">${fmt(v, d.unit)}</span>
    </div>`;
  }).join('');
  for (const [k] of FRONT_MODS) drawPartKnob(k);
}
// small steps on a part with a real E-series walk the series; Shift (or a big step) moves freely along its range
const partNudge = (k, d, e) => { if (realSeries(k) && !e.shiftKey && Math.abs(d) < .05) stepReal(k, Math.sign(d)); else setPartT(k, partT(k) + d, e.shiftKey); };
const partDials = [partsEl, modsEl].map((box) => bindDial(box, {
  pick: (e) => e.target.closest('.knob[role=slider]'), get: partT, set: (k, t, e) => setPartT(k, t, e.shiftKey), nudge: partNudge,
  drag: 180, min: 0, max: 1, focus: true, reset: (k) => applyPart(k, COMPONENTS[k].v), start: (k) => { if (box === partsEl) showDetail(k); },
}));
for (const box of [partsEl, modsEl]) {
box.addEventListener('click', (e) => {
  const lab = e.target.closest('.val'); if (!lab) return;
  const k = lab.dataset.k; if (COMPONENTS[k].fixed) return;
  applyPart(k, COMPONENTS[k].v);
});
}
partsEl.addEventListener('pointerover', (e) => { const p = e.target.closest('.pk'); if (p && !partDials[0].dragging()) showDetail(p.dataset.k); });
partsEl.addEventListener('focusin', (e) => { const p = e.target.closest('.pk'); if (p) showDetail(p.dataset.k); });

