// ---------- knobs ----------
const panel = document.getElementById('panel');
const KNOB_A0 = -150, KNOB_SWEEP = 300; // dial travel in degrees, 0 = straight up
// solid ring (Johnny): a faint ghost track, lit solid up to the pointer, which runs smoothly over the 300° sweep.
// The pointer is a plain 4-wide line from the exact centre to the ring's outer edge; the lit arc is a filled band whose
// end is cut along the pointer's leading edge, so line and arc meet flush as one shape
const knobAngle = (p) => KNOB_A0 + KNOB_SWEEP * Math.min(1, Math.max(0, p));
function knobSVG() {
  // LCD dial: a ghost ring, a lit band up to the value, and an ink line from the centre out to the ring
  return `<svg viewBox="0 0 64 64" aria-hidden="true">
    <path class="trk" d="M19.5 53.65 A25 25 0 1 1 44.5 53.65" fill="none"/>
    <path class="arc" d="" fill="none"/>
    <line class="ptr" x1="32" y1="32" x2="32" y2="3.5"/>
  </svg>`;
}
const KNOB_RI = 21.5, KNOB_RO = 28.5, KNOB_HW = 2; // ring band and half the pointer width, in the 64-unit viewBox
function arcPath(p) {
  const a0 = KNOB_A0, a = knobAngle(p), cx = 32, cy = 32;
  const pt = (deg, r) => { const t = (deg - 90) * Math.PI / 180; return `${(cx + r * Math.cos(t)).toFixed(2)} ${(cy + r * Math.sin(t)).toFixed(2)}`; };
  const end = (r) => a + Math.asin(KNOB_HW / r) * 180 / Math.PI; // where the pointer's leading edge crosses radius r
  const eo = end(KNOB_RO), ei = end(KNOB_RI);
  return `M${pt(a0, KNOB_RO)} A${KNOB_RO} ${KNOB_RO} 0 ${eo - a0 > 180 ? 1 : 0} 1 ${pt(eo, KNOB_RO)} L${pt(ei, KNOB_RI)} A${KNOB_RI} ${KNOB_RI} 0 ${ei - a0 > 180 ? 1 : 0} 0 ${pt(a0, KNOB_RI)} Z`;
}
function knobText(id, p) {
  const c = state.parts;
  switch (id) {
    case 'tuning': return ((p - 0.5) * 2 >= 0 ? '+' : '') + ((p - 0.5) * 24).toFixed(1) + ' st';
    case 'cutoff': return (preview.wCut * c.V_CUT * p * 1000).toFixed(1) + ' mV';
    case 'resonance': return 'k = ' + (c.A_Q21 * p).toFixed(1);
    case 'decay': return fmt(c.R138 + c.VR6 * Math.pow(p, 2.74), 'Ω');
    case 'dist': return 'gain ×' + Math.round(1 + (c.RAT_RD * Math.pow(p, 2.74) + 1) * (1 / c.RAT_RG1 + 1 / c.RAT_RG2));
    case 'filter': return fmt(Math.round(1 / (2 * Math.PI * (c.RAT_RF + 100e3 * p) * c.RAT_CFLT)), 'Hz');
    case 'dlytime': return dlyDiv(p)[0];
    case 'dlyfb': return Math.round(95 * p) + '%';
    case 'dlylo': return fmt(Math.round(dlyLo(p)), 'Hz');
    case 'dlyhi': { const f = dlyHi(p); return f >= 1000 ? (f / 1000).toFixed(1) + ' kHz' : Math.round(f) + ' Hz'; }
    case 'cmpthr': return cmpThrDb(p).toFixed(1) + ' dB';
    case 'cmprng': return Math.round(200 * p) + '%';
    case 'cmpatk': return cmpAtkMs(p).toFixed(1) + ' ms';
    case 'cmphold': return Math.round(cmpHoldMs(p)) + ' ms';
    case 'cmprel': return Math.round(cmpRelMs(p)) + ' ms';
    default: {
      if (id.startsWith('lex')) return lexText(id, p);
      return Math.round(p * 100) + '%';
    }
  }
}
function lexText(id, p) {
  const q = lexParams({ ...state.knobs, [id]: p }), sec = (t) => (t < 10 ? t.toFixed(2) : t.toFixed(1)) + ' s';
  const hz = (f) => f >= 10000 ? 'off' : f >= 1000 ? (f / 1000).toFixed(f < 10000 ? 2 : 1) + ' kHz' : Math.round(f) + ' Hz';
  const chorus = q.P.alg === 'chorus';
  switch (id) {
    case 'lexprog': return 'P' + q.P.n;
    case 'lexbass': return chorus ? Math.round(p * 100) + '%' : sec(q.bass);
    case 'lexmid': return chorus ? Math.round(p * 100) + '%' : sec(q.mid);
    case 'lexxover': return chorus ? Math.round(p * 100) + '%' : hz(q.xover);
    case 'lextreble': return chorus ? Math.round(p * 100) + '%' : hz(q.treble);
    case 'lexdepth': return String(q.depth);
    case 'lexdiff': return String(q.diff);
    case 'lexpre': return Math.round(q.pre) + ' ms';
    case 'lexme': return q.me ? 'amt ' + q.me : 'off';
    case 'lexpitch': return String(q.pitch);
    case 'lexmix': return Math.round(p * 100) + '%';
  }
  return '';
}
// One binding for every dial: vertical drag (Shift = fine), wheel, arrow and Page keys, Home/End, double-click.
// host is the dial itself, or a container whose dials are found by pick(e) and named by data-k.
// set(k, value, event) and nudge(k, delta, event) receive dial units; min/max enable Home/End.
function bindDial(host, { pick = () => host, get, set, nudge = (k, d, e) => set(k, get(k) + d, e), drag = 160, fine = 600, step = .02, page = .1, min, max, reset, start, focus = false }) {
  let d = null;
  const at = (e) => { const el = pick(e); return el && { el, k: el.dataset.k }; };
  host.addEventListener('pointerdown', (e) => {
    const t = at(e); if (!t) return;
    t.el.setPointerCapture(e.pointerId); if (focus) t.el.focus();
    d = { k: t.k, y: e.clientY, v: get(t.k), id: e.pointerId, el: t.el }; start?.(t.k, t.el);
  });
  host.addEventListener('pointermove', (e) => { if (d && e.pointerId === d.id) set(d.k, d.v + (d.y - e.clientY) / (e.shiftKey ? fine : drag), e); });
  const end = () => { if (d) d.el.dispatchEvent(new Event('dialend')); d = null; };
  host.addEventListener('pointerup', end); host.addEventListener('pointercancel', end);
  host.addEventListener('wheel', (e) => { const t = at(e); if (!t) return; e.preventDefault(); nudge(t.k, -Math.sign(e.deltaY) * step, e); }, { passive: false });
  host.addEventListener('keydown', (e) => {
    const t = at(e); if (!t) return;
    const delta = { ArrowUp: step, ArrowRight: step, ArrowDown: -step, ArrowLeft: -step, PageUp: page, PageDown: -page }[e.key];
    if (delta) { e.preventDefault(); nudge(t.k, delta, e); }
    if (min !== undefined && e.key === 'Home') set(t.k, min, e);
    if (max !== undefined && e.key === 'End') set(t.k, max, e);
  });
  if (reset) host.addEventListener('dblclick', (e) => { const t = at(e); if (t) reset(t.k); });
  return { dragging: () => !!d };
}
const knobEls = {};
for (const k of KNOBS) {
  if (k.hidden) continue;
  const wrap = document.createElement('div'); wrap.className = k.fx ? 'kn small' : 'kn';
  wrap.innerHTML = `<div class="knob" tabindex="0" role="slider" aria-label="${k.name}" aria-valuemin="0" aria-valuemax="100" id="knob-${k.id}">${knobSVG()}</div>
    <div class="name">${k.name}</div><div class="ref">${k.ref}</div><div class="val"></div>`;
  (k.fx ? document.getElementById('fx-' + k.fx + '-knobs') : panel).appendChild(wrap);
  const el = wrap.querySelector('.knob');
  knobEls[k.id] = { el, arc: el.querySelector('.arc'), ptr: el.querySelector('.ptr'), val: wrap.querySelector('.val'), name: wrap.querySelector('.name') };
  bindDial(el, { get: () => state.knobs[k.id], set: (_, v) => setKnob(k.id, v), min: 0, max: 1, reset: () => setKnob(k.id, k.def), start: () => wrap.classList.add('active') });
  el.addEventListener('dialend', () => wrap.classList.remove('active'));
}
function drawKnob(id) {
  if (id === 'cmpmode' || id === 'dlypp') { drawCmpMode(); return; }
  const p = state.knobs[id], k = knobEls[id]; if (!k) return;
  k.arc.setAttribute('d', arcPath(p));
  k.ptr.setAttribute('transform', `rotate(${knobAngle(p)} 32 32)`);
  k.el.setAttribute('aria-valuenow', Math.round(p * 100));
  k.val.textContent = knobText(id, p);
  if (id.startsWith('lex')) drawLex();
}
function setKnob(id, p) {
  p = Math.min(1, Math.max(0, p)); state.knobs[id] = p;
  preview.setKnob(id, p); drawKnob(id); send({ type: 'knob', name: id, value: p }); readouts(); saveSession();
}

// ---------- readouts ----------
function readouts() {
  const v = preview, c = state.parts, k = state.knobs;
  const vb = v.wCut * v.vCutPot;
  const envTop = vb + v.wEnv * c.V_ENV * v.envModFrac;
  const rd = c.R138 + c.VR6 * Math.pow(k.decay, 2.74);
  const rows = [
    ['VCO at A (code 33)', fmt(c.F_TUNE * Math.pow(2, (k.tuning - 0.5) * 2) * Math.pow(2, dacVoltage(33, c.VREF, c.R_DAC, c.R2_DAC) - c.CV_TUNE), 'Hz')],
    ['Ramp current at A', fmt(c.F_TUNE * c.C33 * (c.V_HI - c.V_LO), 'A')],
    ['VCF peak, knob only', fmt(Math.round(v.vcfResonantHz(vb)), 'Hz')],
    ['VCF peak, env top', fmt(Math.round(Math.min(v.vcfResonantHz(envTop), 99999)), 'Hz')],
    ['Feedback k / k_crit', (c.A_Q21 * k.resonance).toFixed(1) + ' / 17.0'],
    ['Env decay to 10%', (2.3026 * rd * c.C62 * 1000).toFixed(0) + ' ms'],
    ['Accent decay to 10%', (2.3026 * c.R138 * c.C62 * 1000).toFixed(0) + ' ms'],
    ['Slide time constant', (c.R93 * c.C35 * 1000).toFixed(1) + ' ms'],
    ['Square HPF corner', fmt(+(1 / (2 * Math.PI * c.C10 * c.R_SQB)).toFixed(1), 'Hz')],
  ];
  document.getElementById('readouts').innerHTML = rows.map(([a, b]) => `<dt>${a}</dt><dd>${b}</dd>`).join('');
}

