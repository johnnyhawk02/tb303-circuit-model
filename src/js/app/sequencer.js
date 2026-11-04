// ---------- sequencer: piano roll ----------
// a note is drawn as a key row (C..B) plus an octave lane; code = 24 + key + 12 * octave
const seq = document.getElementById('seq');
const KEY_ROWS = [11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0], OCT_ROWS = [2, 1, -1, -2];
const BLACK = new Set([1, 3, 6, 8, 10]);
const splitCode = (c) => { const o = Math.floor((c - 24) / 12); return { o, k: c - 24 - 12 * o }; };
const clampCode = (c) => Math.min(59, Math.max(0, c));
let seqCols = [], tail = []; // tail keeps steps removed by shortening, so lengthening brings them back
// the grid always shows at least 32 steps; columns past the pattern's length are dimmed, and pressing one
// extends the pattern up to that step (with rests, or the steps you shortened away) and sets that cell
const SEQ_SHOW = 32;
function renderSeq() {
  const P = state.pattern, n = P.length, cols = Math.max(SEQ_SHOW, n), focusId = document.activeElement?.closest?.('#seq') ? document.activeElement.id : '';
  seqCols = P.map(() => []);
  const ghost = Array.from({ length: cols - n }, (_, j) => n + j);
  const bar = (i) => (i % 4 === 3 && i < cols - 1 ? ' bar' : '') + (i === n - 1 && n < cols ? ' end' : '');
  const gcell = (cls, t, v, i, lab) => `<button type="button" class="cell ghost${cls}${bar(i)}" data-gcol="${i}" data-gt="${t}" data-v="${v}" tabindex="-1" aria-label="Extend to step ${i + 1}, ${lab}"></button>`;
  let h = '<div class="rh">STEP</div>' + P.map((_, i) => `<div class="hd${bar(i)}" data-col="${i}">${i % 8 === 0 ? i + 1 : ''}</div>`).join('')
    + ghost.map(i => `<div class="hd ghost${bar(i)}">${i % 8 === 0 ? i + 1 : ''}</div>`).join('');
  OCT_ROWS.forEach((o, r) => {
    const last = r === OCT_ROWS.length - 1 ? ' gap' : '', ol = `octave ${o > 0 ? '+' : '−'}${Math.abs(o)}`;
    h += `<div class="rh${last}">${o > 0 ? '+' : '−'}${Math.abs(o)}</div>` + P.map((s, i) => {
      const on = splitCode(s.code).o === o;
      return `<button type="button" class="cell${on && s.gate ? ' on' : ''}${bar(i)}${last}" id="so${o}_${i}" data-col="${i}" data-t="o" data-v="${o}" aria-pressed="${on}" aria-label="Step ${i + 1} ${ol}"></button>`;
    }).join('') + ghost.map(i => gcell(last, 'o', o, i, ol)).join('');
  });
  KEY_ROWS.forEach((k, r) => {
    const cls = (BLACK.has(k) ? ' blk' : '') + (r === KEY_ROWS.length - 1 ? ' gap' : '');
    h += `<div class="rh${cls}">${NOTE[k]}</div>` + P.map((s, i) => {
      const on = splitCode(s.code).k === k;
      return `<button type="button" class="cell${cls}${on && s.gate ? ' on' : ''}${bar(i)}" id="sk${k}_${i}" data-col="${i}" data-t="k" data-v="${k}" aria-pressed="${on && s.gate}" aria-label="Step ${i + 1} ${NOTE[k]}${on && !s.gate ? ' (rest)' : ''}"></button>`;
    }).join('') + ghost.map(i => gcell(cls, 'k', k, i, NOTE[k])).join('');
  });
  for (const [key, lab] of [['slide', 'SLIDE'], ['accent', 'ACC']]) {
    h += `<div class="rh">${lab}</div>` + P.map((s, i) =>
      `<button type="button" class="cell${s[key] ? ' on' : ''}${bar(i)}" id="s${key}_${i}" data-col="${i}" data-t="${key}" aria-pressed="${s[key]}" aria-label="Step ${i + 1} ${key}"></button>`).join('')
      + ghost.map(i => gcell('', key, 0, i, key)).join('');
  }
  seq.innerHTML = h; seq.style.setProperty('--steps', cols);
  for (const el of seq.querySelectorAll('[data-col]')) seqCols[+el.dataset.col].push(el);
  for (const el of seqCols[lastStep] || []) el.classList.add('cur');
  if (focusId) document.getElementById(focusId)?.focus();
  renderSide();
}
// click (or keyboard) toggles one cell; press and drag paints: the first cell's new state is laid on every
// step the stroke crosses. In the note rows the stroke follows the pointer up and down, so a line can be drawn.
function applyCell(b) {
  const s = state.pattern[+b.dataset.col], v = +b.dataset.v, { o, k } = splitCode(s.code);
  if (b.dataset.t === 'k') { if (k === v && s.gate) s.gate = false; else { s.code = clampCode(24 + v + 12 * o); s.gate = true; } }
  else if (b.dataset.t === 'o') s.code = clampCode(24 + k + 12 * (o === v ? 0 : v));
  else s[b.dataset.t] = !s[b.dataset.t];
}
let paint = null, paintedClick = false;
function paintCell(b) {
  const col = +b.dataset.col, s = state.pattern[col]; if (!s) return false;
  const t = b.dataset.t, v = +b.dataset.v, { o, k } = splitCode(s.code), before = s.code + ':' + s.gate + s.accent + s.slide;
  if (paint.t === 'k') {
    if (paint.on) { if (t === 'k') { s.code = clampCode(24 + v + 12 * o); s.gate = true; } else return false; }
    else s.gate = false;
  } else if (paint.t === 'o') { if (t !== 'o') return false; s.code = clampCode(24 + k + 12 * paint.oct); }
  else { if (t !== paint.t) return false; s[t] = paint.on; }
  return before !== s.code + ':' + s.gate + s.accent + s.slide;
}
// grow the pattern to `len` steps, bringing back steps removed by shortening first, then rests on the last note
function extendTo(len) {
  const P = state.pattern;
  while (P.length < Math.min(64, len)) P.push(tail.shift() || restStep(P[P.length - 1].code));
}
// redraw one column's cells in place (cheap), instead of rebuilding the whole roll on every pointer move
function syncCol(i) {
  const s = state.pattern[i]; if (!s) return;
  const { o, k } = splitCode(s.code);
  for (const el of seqCols[i] || []) {
    const t = el.dataset.t; if (!t) continue;
    const v = +el.dataset.v, on = t === 'k' ? k === v && s.gate : t === 'o' ? o === v && s.gate : !!s[t];
    el.classList.toggle('on', on);
    el.setAttribute('aria-pressed', String(t === 'o' ? o === v : on));
    if (t === 'k') el.setAttribute('aria-label', `Step ${i + 1} ${NOTE[v]}${k === v && !s.gate ? ' (rest)' : ''}`);
  }
}
// A fast drag only gets a pointer event every few steps, so the stroke is followed as a line: from the last point to the
// new one (and through any coalesced points between), sampled at under half a cell, every cell it crosses is painted.
// Cell boxes are measured once per stroke (client coordinates, so the --k screen scaling is already in them).
let paintBoxes = [], paintStep = 4, paintFrame = 0;
function measurePaint() {
  paintBoxes = [...seq.querySelectorAll('button[data-t], button[data-gcol]')].map(el => ({ el, r: el.getBoundingClientRect() }));
  const w = paintBoxes.reduce((m, b) => Math.min(m, b.r.width, b.r.height), Infinity);
  paintStep = Math.max(1, Math.min(8, (isFinite(w) ? w : 8) / 2.5));
}
function cellAt(x, y) {
  if (!paintBoxes.length || !paintBoxes[0].el.isConnected) measurePaint();
  for (const b of paintBoxes) if (x >= b.r.left && x < b.r.right && y >= b.r.top && y < b.r.bottom) return b.el;
  return null;
}
function paintAt(x, y) {
  let b = cellAt(x, y); if (!b) return;
  if (b.dataset.gcol) { // dragged past the pattern's end: extend it, as a press there does
    const i = +b.dataset.gcol, t = b.dataset.gt, v = b.dataset.v;
    if (t !== paint.t && !(paint.t === 'k' && !paint.on)) return;
    extendTo(i + 1); renderSeq(); measurePaint(); paint.dirty = true;
    b = document.getElementById(t === 'o' ? `so${v}_${i}` : t === 'k' ? `sk${v}_${i}` : `s${t}_${i}`); if (!b) return;
  }
  if (paintCell(b)) { syncCol(+b.dataset.col); paint.dirty = true; }
}
function flushPaint() {
  paintFrame = 0; if (!paint || !paint.dirty) return;
  paint.dirty = false; sendPattern(); // heard straight away; saving and the bank display wait for the end of the stroke
  paint.changed = true;
}
seq.addEventListener('pointerdown', (e) => {
  const g = e.target.closest('button[data-gcol]');
  if (g && e.button <= 0) { // a dimmed step past the end: extend, then treat it as a press on the real cell
    e.preventDefault(); const i = +g.dataset.gcol, t = g.dataset.gt, v = g.dataset.v;
    extendTo(i + 1); renderSeq();
    const id = t === 'o' ? `so${v}_${i}` : t === 'k' ? `sk${v}_${i}` : `s${t}_${i}`, b = document.getElementById(id);
    if (b) b.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, cancelable: true, pointerId: e.pointerId, pointerType: e.pointerType, button: 0, clientX: e.clientX, clientY: e.clientY }));
    else pushPattern();
    return;
  }
  const b = e.target.closest('button[data-t]'); if (!b || e.button > 0) return;
  e.preventDefault(); applyCell(b); paintedClick = true;
  const s = state.pattern[+b.dataset.col], t = b.dataset.t;
  paint = { t, id: e.pointerId, on: t === 'k' ? s.gate : t === 'o' ? true : s[t], oct: splitCode(s.code).o, x: e.clientX, y: e.clientY };
  try { if (e.isTrusted) seq.setPointerCapture(e.pointerId); } catch {} // keep the stroke's events however the cells change
  measurePaint(); syncCol(+b.dataset.col); pushPattern();
});
addEventListener('pointermove', (e) => {
  if (!paint || e.pointerId !== paint.id) return;
  if (e.pointerType === 'mouse' && !(e.buttons & 1)) { endPaint(e); return; } // button released somewhere we didn't hear: stop painting
  const pts = e.getCoalescedEvents?.() || []; if (!pts.length) pts.push(e);
  for (const p of pts) {
    const dx = p.clientX - paint.x, dy = p.clientY - paint.y, n = Math.max(1, Math.ceil(Math.hypot(dx, dy) / paintStep));
    for (let j = 1; j <= n; j++) paintAt(paint.x + dx * j / n, paint.y + dy * j / n);
    paint.x = p.clientX; paint.y = p.clientY;
  }
  if (paint.dirty && !paintFrame) paintFrame = requestAnimationFrame(flushPaint);
});
function endPaint(e) {
  if (!paint || e.pointerId !== paint.id) return;
  if (e.type === 'pointerup' && (e.clientX !== paint.x || e.clientY !== paint.y)) { // the last stretch of the line
    const dx = e.clientX - paint.x, dy = e.clientY - paint.y, n = Math.max(1, Math.ceil(Math.hypot(dx, dy) / paintStep));
    for (let j = 1; j <= n; j++) paintAt(paint.x + dx * j / n, paint.y + dy * j / n);
  }
  const done = paint.dirty || paint.changed; paint = null; paintBoxes = [];
  if (paintFrame) { cancelAnimationFrame(paintFrame); paintFrame = 0; }
  if (done) { renderSeq(); pushPattern(); }
}
addEventListener('pointerup', endPaint); addEventListener('pointercancel', endPaint);
seq.addEventListener('click', (e) => {
  if (paintedClick) { paintedClick = false; if (e.detail !== 0) return; } // the press already did it
  const b = e.target.closest('button[data-t]'); if (!b) return;
  applyCell(b); renderSeq(); pushPattern();
});
// arrow keys move around the grid
seq.addEventListener('keydown', (e) => {
  const b = e.target.closest('button[data-t]'); if (!b) return;
  const d = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key]; if (!d) return;
  const cells = [...seq.querySelectorAll('button[data-t]')], n = state.pattern.length, idx = cells.indexOf(b);
  const r = Math.floor(idx / n), c = idx % n, rows = cells.length / n;
  const t = cells[Math.min(rows - 1, Math.max(0, r + d[1])) * n + Math.min(n - 1, Math.max(0, c + d[0]))];
  if (t) { e.preventDefault(); t.focus(); }
});

// ---------- side controls ----------
const sideIn = Object.fromEntries([...document.querySelectorAll('.sq-side input[data-a]')].map(el => [el.dataset.a, el]));
function renderSide() {
  document.getElementById('o-len').textContent = sideIn.len.value = state.pattern.length;
  sideIn.gate.value = Math.round(state.gate * 100);
  document.getElementById('o-gate').textContent = Math.round(state.gate * 100) + '%';
}
function sendGroove() { send({ type: 'groove', shuffle: state.shuffle, gate: state.gate }); }
const restStep = (code) => ({ code, gate: false, accent: false, slide: false });
// sliders, as in the Shyoy: Length and Step len set values; Shift and Transpose spring back to centre when let go
let sprBase = null;
const sprReset = () => { sprBase = null; for (const a of ['shift', 'tr']) { sideIn[a].value = 0; document.getElementById('o-' + a).textContent = '0'; } };
for (const a of ['shift', 'tr']) {
  const el = sideIn[a];
  const grab = () => { if (!sprBase) { pushUndo(); sprBase = state.pattern.map(st => ({ ...st })); } };
  el.addEventListener('pointerdown', grab); el.addEventListener('focus', grab);
  el.addEventListener('change', () => { if (el.matches(':focus-visible')) return; sprReset(); });
  el.addEventListener('pointerup', sprReset); el.addEventListener('pointercancel', sprReset); el.addEventListener('blur', sprReset);
}
document.querySelector('.sq-side').addEventListener('input', (e) => {
  const el = e.target.closest('input[data-a]'); if (!el) return;
  const v = +el.value, P = state.pattern;
  switch (el.dataset.a) {
    case 'len': while (P.length > v && P.length > 1) tail.unshift(P.pop()); extendTo(v); break;
    case 'gate': state.gate = v / 100; sendGroove(); break;
    case 'shift': {
      if (!sprBase || sprBase.length !== P.length) sprBase = P.map(st => ({ ...st }));
      const n = sprBase.length, k = ((v % n) + n) % n;
      state.pattern = sprBase.map((_, i) => ({ ...sprBase[(i - k + n) % n] }));
      renderSeq(); pushPattern(); document.getElementById('o-shift').textContent = (v > 0 ? '+' : '') + v; return;
    }
    case 'tr': {
      if (!sprBase || sprBase.length !== P.length) sprBase = P.map(st => ({ ...st }));
      const codes = sprBase.map(st => st.code), lo = Math.min(...codes), hi = Math.max(...codes);
      const d = Math.max(-lo, Math.min(59 - hi, v));
      state.pattern = sprBase.map(st => ({ ...st, code: st.code + d }));
      renderSeq(); pushPattern(); document.getElementById('o-tr').textContent = (d > 0 ? '+' : '') + d; return;
    }
  }
  renderSeq(); pushPattern();
});
// Undo next to the pattern tools: the same undo as Functions › Undo
{
  const fu = document.getElementById('fn-undo'), pu = document.getElementById('p-undo');
  pu.onclick = () => fu.click();
  new MutationObserver(() => { pu.disabled = fu.disabled; }).observe(fu, { attributes: true, attributeFilter: ['disabled'] });
}
// Randomise accents, slides or octaves: each rewrites only its own property on the notes (steps with the gate on) and
// leaves the notes and the other properties alone; rests are untouched. Again for another roll; Undo puts it back.
const RND_OCT = [[-1, 3], [0, 5], [1, 2], [2, 0.4], [-2, 0.4]]; // octave lane, weight: mostly 0 and -1, some +1, a rare extreme
const rndOct = () => { let r = Math.random() * RND_OCT.reduce((a, [, w]) => a + w, 0); for (const [o, w] of RND_OCT) if ((r -= w) < 0) return o; return 0; };
function randomise(kind) {
  const P = state.pattern; if (!P.some(st => st.gate)) { sqSay('No notes to change: put some notes in first.'); return; }
  pushUndo();
  for (const st of P) {
    if (!st.gate) continue;
    if (kind === 'accent') st.accent = Math.random() < 0.3;
    else if (kind === 'slide') st.slide = Math.random() < 0.25;
    else { const { k } = splitCode(st.code); st.code = clampCode(24 + k + 12 * rndOct()); }
  }
  renderSeq(); pushPattern();
  sqSay(`New random ${kind === 'accent' ? 'accents' : kind === 'slide' ? 'slides' : 'octaves'}. Again for another roll; Undo puts it back.`);
}
// Duplicate steps: repeat the pattern until it fills the next of 16, 32 or 64 steps (a 5-step riff becomes 16, then 32)
document.getElementById('p-dup').onclick = () => {
  const P = state.pattern, n = P.length, to = [16, 32, 64].find(x => x > n);
  if (!to) { sqSay('Already at the 64-step maximum.'); return; }
  pushUndo();
  state.pattern = Array.from({ length: to }, (_, i) => ({ ...P[i % n] })); tail = [];
  renderSeq(); pushPattern();
  sqSay(`Repeated ${n} step${n === 1 ? '' : 's'} out to ${to}. Undo puts it back.`);
};
// Shuffle notes: deal the steps out in a new random order (each note keeps its accent and slide); tap again for another
function shuffleSteps() {
  const P = state.pattern; if (P.length < 2) return;
  pushUndo();
  const before = JSON.stringify(P);
  for (let tries = 0; tries < 8 && JSON.stringify(P) === before; tries++)
    for (let i = P.length - 1; i > 0; i--) { const j = Math.floor(Math.random() * (i + 1)); [P[i], P[j]] = [P[j], P[i]]; }
  tail = []; renderSeq(); pushPattern();
  sqSay(`Shuffled ${P.length} steps. Again for another order; Undo puts it back.`);
}
drawGen();
document.querySelector('.sq-gen').addEventListener('input', (e) => {
  const el = e.target.closest('input[data-g]'); if (!el) return;
  if (el.dataset.g === 'dens') gen.dens = Math.min(GEN_DENS.length - 1, Math.max(0, +el.value)); else gen.bars = Math.min(4, Math.max(1, +el.value));
  store.set('gen', gen); drawGen();
});
document.getElementById('rand').onclick = () => {
  pushUndo();
  // Generate keeps the pattern's length (Johnny) but writes all 64 steps: a phrase of Bars bars, repeated out to 64,
  // so lengthening the pattern afterwards brings in more of the generated riff instead of rests
  const g = generatePattern(gen.bars * 16, GEN_DENS[gen.dens]), n = state.pattern.length;
  const full = Array.from({ length: 64 }, (_, i) => ({ ...g.pattern[i % g.pattern.length] }));
  state.pattern = full.slice(0, n); tail = full.slice(n); renderSeq(); pushPattern();
  const d = g.did.length > 1 ? g.did.slice(0, -1).join(', ') + ' and ' + g.did.at(-1) : g.did[0];
  sqSay(`From ${g.src}: ${d}. Undo puts the old one back.`);
};


// The cross under the roll (Johnny): up and down move every note a semitone (Shift-click for an octave), left and right
// rotate the pattern a step. Moving notes is a real transpose, so B up a semitone becomes C in the next octave lane. If
// any note would go past the top or bottom of the range nothing moves (so the riff keeps its shape) and a message says so.
function nudgeNotes(d) {
  const P = state.pattern, notes = P.filter(st => st.gate);
  if (!notes.length) { sqSay('No notes to move: put some notes in first.'); return; }
  if (notes.some(st => st.code + d < 0 || st.code + d > 59)) { sqSay(`Already at the ${d > 0 ? 'top' : 'bottom'} of the range.`); return; }
  pushUndo();
  for (const st of P) st.code = clampCode(st.code + d);
  renderSeq(); pushPattern();
}
function rotateSteps(k) {
  const P = state.pattern, n = P.length; if (n < 2) return;
  pushUndo();
  state.pattern = P.map((_, i) => P[(i - k + n) % n]);
  renderSeq(); pushPattern();
}
document.querySelector('.dpad').addEventListener('click', (e) => {
  const b = e.target.closest('button'); if (!b) return;
  if (b.dataset.nudge) nudgeNotes(+b.dataset.nudge * (e.shiftKey ? 12 : 1));
  else if (b.dataset.rot) rotateSteps(+b.dataset.rot);
});

// Alter accents, slides or octaves: a variation rather than a new roll. About a quarter of the notes (at least one) get
// the change: an accent or slide flipped on or off, or an octave jump up or down within range. Undo puts it back.
function alterLane(kind) {
  const notes = state.pattern.filter(st => st.gate); if (!notes.length) { sqSay('No notes to vary: put some notes in first.'); return; }
  pushUndo();
  const pick = [...notes].sort(() => Math.random() - 0.5).slice(0, Math.max(1, Math.round(notes.length / 4)));
  for (const st of pick) {
    if (kind === 'octave') { const up = st.code + 12 <= 59 && (st.code - 12 < 0 || Math.random() < 0.5); st.code += up ? 12 : -12; }
    else st[kind] = !st[kind];
  }
  renderSeq(); pushPattern();
  const n = pick.length, w = { accent: ['accent', 'accents'], slide: ['slide', 'slides'], octave: ['octave', 'octaves'] }[kind];
  sqSay(`Altered ${n} ${w[n > 1 ? 1 : 0]}. Again for more; Undo puts it back.`);
}
