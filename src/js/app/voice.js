// the waveform switch sits at the left of the voice panel; component randomise and its undo at the right
panel.insertAdjacentHTML('afterbegin', `<div class="kn wave-kn"><span class="name">Waveform</span>
  <button type="button" class="switch" id="wave-sw" role="switch" aria-checked="false" aria-label="Square wave"><span class="thumb"></span></button>
  <span class="val" id="wave-val">Saw</span></div>`);
const waveSw = document.getElementById('wave-sw');
panel.insertAdjacentHTML('beforeend', `<div class="kn parts-kn"><span class="name">Components</span>
  <div class="trim" title="TM3 filter frequency trimmer, 470K in series with the cutoff pot. Clockwise: brighter, and the cutoff knob sweeps wider. Anticlockwise: darker and narrower. Double-click to reset.">
    <div class="knob" data-k="trim" tabindex="0" role="slider" aria-label="Filter trim (TM3)" aria-valuemin="0" aria-valuemax="100">${knobSVG()}</div>
    <span class="tname">Filter trim</span><span class="tval" id="trim-val"></span></div>
  <button type="button" class="mini" id="parts-rand" title="New machine: every part lands somewhere inside its real tolerance (±5% resistors, ±10–20% capacitors), like another unit off the line">New unit</button>
  <button type="button" class="mini" id="parts-bend" title="Messed up: every part bent well outside spec, circuit-bending style. Tuning parts are left alone so it stays in tune">Messed up</button>
  <button type="button" class="mini" id="parts-undo" disabled>Undo</button></div>`);
// TM3 filter trimmer: the factory cutoff calibration pot (470K in series with R73), shown as a little trimmer under the voice
// knobs. Clockwise = less resistance = brighter. The resistance-to-pitch law is very uneven (the last 50K is wild), so the dial
// is spread evenly in semitones at mid cutoff: fully anticlockwise is 470K, centre is the factory setting, fully clockwise 50K.
// The readout is the shift at the current cutoff setting, which is how much you actually hear.
const trimEl = panel.querySelector('.trim .knob');
const TRIM_HI = 470e3, TRIM_LO = 50e3;
function trimShift(tm, cut) { // semitones the cutoff moves vs the factory TM3 setting, at cutoff knob position cut
  const c = state.parts, G = 1 / c.R72 + 1 / c.R63 + 1 / c.R71, w = (r) => { const g = 1 / (c.R73 + r); return g / (G + g); };
  return (w(tm) - w(COMPONENTS.TM3.v)) * (c.V_CUT0 + c.V_CUT * cut) / VT / Math.LN2 * 12;
}
function trimFromShift(st) { // inverse of trimShift at mid cutoff
  const c = state.parts, G = 1 / c.R72 + 1 / c.R63 + 1 / c.R71, g0 = 1 / (c.R73 + COMPONENTS.TM3.v);
  const w = g0 / (G + g0) + st * Math.LN2 / 12 * VT / (c.V_CUT0 + c.V_CUT * 0.5);
  return Math.min(TRIM_HI, Math.max(TRIM_LO, (1 - w) / (w * G) - c.R73));
}
function trimT(tm) { const s = trimShift(tm, 0.5), end = trimShift(s < 0 ? TRIM_HI : TRIM_LO, 0.5); return Math.min(1, Math.max(0, 0.5 + 0.5 * s / end * (s < 0 ? -1 : 1))); }
function trimTM(t) { t = Math.min(1, Math.max(0, t)); const end = trimShift(t < 0.5 ? TRIM_HI : TRIM_LO, 0.5); return +trimFromShift(Math.abs(t - 0.5) * 2 * end).toPrecision(3); }
function drawTrim() {
  const t = trimT(state.parts.TM3);
  trimEl.querySelector('.arc').setAttribute('d', arcPath(t));
  trimEl.querySelector('.ptr').setAttribute('transform', `rotate(${KNOB_A0 + KNOB_SWEEP * t} 32 32)`);
  trimEl.setAttribute('aria-valuenow', Math.round(t * 100));
  const el = document.getElementById('trim-val'); if (!el) return;
  const st = trimShift(state.parts.TM3, state.knobs.cutoff), changed = Math.abs(state.parts.TM3 / COMPONENTS.TM3.v - 1) > 1e-6;
  el.textContent = (st >= -0.05 ? '+' : '') + st.toFixed(1) + ' st'; el.classList.toggle('changed', changed);
  trimEl.title = `TM3 ${fmt(state.parts.TM3, 'Ω')}`;
}
bindDial(trimEl, { get: () => trimT(state.parts.TM3), set: (_, t) => { applyPart('TM3', trimTM(t)); drawTrim(); }, drag: 240, fine: 800, focus: true,
  reset: () => { applyPart('TM3', COMPONENTS.TM3.v); drawTrim(); } });
drawTrim();
const partsUndo = [];
function syncPartsUndo() { document.getElementById('parts-undo').disabled = !partsUndo.length; }
document.getElementById('parts-rand').onclick = () => FUNCTIONS.parts();
document.getElementById('parts-bend').onclick = () => FUNCTIONS.parts(true);
document.getElementById('parts-undo').onclick = () => {
  const u = partsUndo.pop(); if (!u) return;
  applySound({ ...soundSnap(), parts: u }); syncPartsUndo(); sqSay('Part values put back.');
};
function drawWave() { waveSw.setAttribute('aria-checked', state.wave === 'square'); document.getElementById('wave-val').textContent = state.wave === 'square' ? 'Square' : 'Saw'; }
waveSw.onclick = () => {
  const w = state.wave === 'square' ? 'saw' : 'square';
  state.wave = w; preview.waveform = w; drawWave(); send({ type: 'wave', value: w }); saveSession();
};

