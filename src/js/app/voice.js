// the waveform switch sits at the left of the voice panel; component randomise and its undo at the right
panel.insertAdjacentHTML('afterbegin', `<div class="kn wave-kn"><span class="name">Waveform</span>
  <button type="button" class="switch" id="wave-sw" role="switch" aria-checked="false" aria-label="Square wave"><span class="thumb"></span></button>
  <span class="val" id="wave-val">Saw</span></div>`);
const waveSw = document.getElementById('wave-sw');
panel.insertAdjacentHTML('beforeend', `<div class="kn parts-kn"><span class="name">Components</span>
  <div class="trim" title="TM3 filter frequency trimmer, 470K in series with the cutoff pot. Down: the cutoff knob reaches higher and sweeps wider. Up: lower and narrower. Double-click to reset.">
    <div class="knob" data-k="TM3" tabindex="0" role="slider" aria-label="Filter trim (TM3)" aria-valuemin="0" aria-valuemax="100">${knobSVG()}</div>
    <span class="tname">Filter trim</span><span class="tval" id="trim-val"></span></div>
  <button type="button" class="mini" id="parts-rand" title="New machine: every part lands somewhere inside its real tolerance (±5% resistors, ±10–20% capacitors), like another unit off the line">New unit</button>
  <button type="button" class="mini" id="parts-bend" title="Messed up: every part bent well outside spec, circuit-bending style. Tuning parts are left alone so it stays in tune">Messed up</button>
  <button type="button" class="mini" id="parts-undo" disabled>Undo</button></div>`);
// TM3 filter trimmer: the factory cutoff calibration pot (470K in series with R73), shown as a little trimmer under the voice knobs
const trimEl = panel.querySelector('.trim .knob');
function drawTrim() {
  drawPartKnob('TM3');
  const el = document.getElementById('trim-val'); if (!el) return;
  el.textContent = fmt(state.parts.TM3, 'Ω'); el.classList.toggle('changed', Math.abs(state.parts.TM3 / COMPONENTS.TM3.v - 1) > 1e-6);
}
bindDial(trimEl, { get: () => partT('TM3'), set: (_, t) => { setPartT('TM3', t); drawTrim(); }, drag: 240, fine: 800, focus: true,
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

