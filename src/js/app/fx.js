// ---------- scope ----------
const cv = document.getElementById('scope'), g = cv.getContext('2d');
function drawStatic(data) {
  const W = cv.width, H = cv.height, cs = getComputedStyle(document.documentElement);
  g.clearRect(0, 0, W, H);
  g.strokeStyle = cs.getPropertyValue('--line'); g.lineWidth = 1;
  for (let i = 1; i < 10; i++) { g.beginPath(); g.moveTo(i * W / 10, 0); g.lineTo(i * W / 10, H); g.stroke(); }
  for (let i = 1; i < 4; i++) { g.beginPath(); g.moveTo(0, i * H / 4); g.lineTo(W, i * H / 4); g.stroke(); }
  if (!data) return;
  g.strokeStyle = cs.getPropertyValue('--hi'); g.lineWidth = 2; g.beginPath();
  for (let i = 0; i < data.length; i++) { const x = i / (data.length - 1) * W, y = H / 2 - data[i] * H * 0.45; i ? g.lineTo(x, y) : g.moveTo(x, y); }
  g.stroke();
}
let buf = null;
function drawScope() {
  if (analyser) {
    buf ||= new Float32Array(analyser.fftSize); analyser.getFloatTimeDomainData(buf);
    let t = 0; for (let i = 1; i < buf.length / 2; i++) if (buf[i - 1] < 0 && buf[i] >= 0) { t = i; break; }
    drawStatic(buf.subarray(t, t + 1024));
  }
  requestAnimationFrame(drawScope);
}
// static preview so the scope is not empty before Play
(function previewTrace() {
  const v = new TB303Voice(44100); for (const k of KNOBS) v.setKnob(k.id, state.knobs[k.id]);
  v.noteOn(21, false, false); const b = new Float32Array(4000); v.process(b);
  let t = 2000; while (t < 3000 && !(b[t - 1] < 0 && b[t] >= 0)) t++;
  drawStatic(b.subarray(t, t + 1000));
})();

// ---------- reverb slot: Lexicon 224, plate, spring, convolution ----------
// each unit's program buttons: [knob, programs, number, short name, full name]
const REV_PROGS = {
  lex: ['lexprog', LEX_PROGRAMS.map(P => [P.n, P.short, P.name])],
  plate: ['plateprog', PLATE_PROGRAMS.map((P, i) => [i + 1, P.short, P.name])],
  spring: ['sprprog', SPRING_PROGRAMS.map((P, i) => [i + 1, P.short, P.name])],
  conv: ['cnvprog', CNV_PROGRAMS.map((P, i) => [i + 1, P.short, P.name])],
};
const REV_NOTES = {
  plate: (q) => `Jon Dattorro's published plate (JAES 1997): four input diffusers into a figure-eight tank with modulated allpasses. ${q.P.name}: every delay scaled ×${q.P.size}. Damping is the high cut inside the tank, Bandwidth the one before it.`,
  spring: (q) => `A dispersive spring model after Välimäki, Parker and Abel (JAES 2010): each spring is a delay loop through ${SPRING_STAGES} stretched allpasses, so every echo smears into the falling drip. ${q.P.name}, transit ${q.P.ms.join(' / ')} ms. Dwell drives the tank, Tension lengthens the chirp.`,
  conv: (q) => {
    if (q.P.ir) {
      const m = irMeta?.irs.find(x => x.id === q.P.ir);
      return (m ? `${m.character} ${m.credit}, ${m.licence}. ` : irFailed ? 'The impulse responses could not be loaded. ' : 'Recorded impulse response; it loads when audio starts. ') +
        'Length trims the response (full at the top). Partitioned FFT convolution, one 512-sample block late, which is the shortest predelay.';
    }
    return `Partitioned FFT convolution with a generated impulse response: seeded noise with the highs dying first${q.P.er.length ? ', a build-up and early reflections' : ''}. ${q.P.gate ? 'Decay sets the gate length. ' : ''}The wet signal is one 512-sample block late, which is the shortest predelay.`;
  },
};
{
  const el = document.getElementById('rev-type');
  el.innerHTML = REV_TYPES.map((T, i) => `<button type="button" role="radio" data-t="${i}" title="${T.name}">${T.short}</button>`).join('');
  el.addEventListener('click', (e) => { const b = e.target.closest('button[data-t]'); if (b) setKnob('revtype', b.dataset.t / 3); });
  document.getElementById('lex-progs').addEventListener('click', (e) => {
    const b = e.target.closest('button[data-i]'); if (!b) return;
    const [knob, list] = REV_PROGS[REV_TYPES[revType(state.knobs)].id]; setKnob(knob, list.length > 1 ? b.dataset.i / (list.length - 1) : 0);
  });
}
// recorded impulse responses for Conv (irs/irs.json): the list loads when Conv is shown, the samples when audio is running,
// decoded at the audio rate on this thread and handed to the engine
let irMeta = null, irFailed = false, irMetaP = null, irSentTo = null;
function loadIRMeta() {
  irMetaP ||= fetch('irs/irs.json').then(r => r.json()).then(m => { irMeta = m; drawLex(); return m; }).catch(() => { irFailed = true; irMetaP = null; drawLex(); return null; });
  return irMetaP;
}
async function loadIRs() {
  if (!ctx || irSentTo === ctx) return; irSentTo = ctx;
  const m = await loadIRMeta(); if (!m) { irSentTo = null; return; }
  for (const ir of m.irs) {
    try {
      const buf = await ctx.decodeAudioData(await (await fetch('irs/' + ir.file)).arrayBuffer());
      const L = buf.getChannelData(0), R = buf.numberOfChannels > 1 ? buf.getChannelData(1) : L;
      send({ type: 'ir', id: ir.id, L: L.slice(), R: R.slice() });
    } catch (e) { console.warn('IR', ir.id, e && e.message); }
  }
}
const LEX_CHORUS_NAMES = { lexbass: 'Pair 1', lexmid: 'Pair 2', lexxover: 'Pair 3', lextreble: 'Pair 4', lexdepth: 'Wander' };
let revShown = '';
function drawLex() {
  const progs = document.getElementById('lex-progs'); if (!progs || !knobEls.lexmix) return;
  const t = revType(state.knobs), T = REV_TYPES[t], [pk, list] = REV_PROGS[T.id];
  for (const b of document.getElementById('rev-type').children) b.setAttribute('aria-checked', +b.dataset.t === t);
  if (revShown !== T.id) { // this unit's program buttons and dials
    revShown = T.id;
    progs.style.setProperty('--n', list.length);
    progs.innerHTML = list.map(([n, short, name], i) => `<button type="button" role="radio" data-i="${i}" aria-checked="false" title="${name}"><b>${n}</b>${short}</button>`).join('');
    for (const k of KNOBS) if (k.fx === 'lex' && knobEls[k.id]) knobEls[k.id].wrap.hidden = (k.rev || 'lex') !== T.id;
  }
  const sel = revProg(state.knobs[pk], list.length);
  for (const b of progs.children) b.setAttribute('aria-checked', +b.dataset.i === sel);
  const lcd = document.getElementById('lex-lcd'), note = document.getElementById('lex-note');
  if (T.id === 'conv') { loadIRMeta(); loadIRs(); }
  if (T.id !== 'lex') {
    const q = { plate: plateParams, spring: springParams, conv: convParams }[T.id](state.knobs);
    lcd.textContent = `${sel + 1} ${q.P.name.toUpperCase()}`;
    for (const k of KNOBS) if (k.rev === T.id && knobEls[k.id]) {
      knobEls[k.id].name.textContent = k.id === 'cnvdecay' ? (q.P.gate ? 'Gate' : q.P.ir ? 'Length' : 'Decay') : k.name;
      knobEls[k.id].val.textContent = knobText(k.id, state.knobs[k.id]);
    }
    note.textContent = REV_NOTES[T.id](q);
    return;
  }
  const q = lexParams(state.knobs), chorus = q.P.alg === 'chorus';
  for (const k of KNOBS) if (k.fx === 'lex' && !k.rev && knobEls[k.id]) { const e = knobEls[k.id]; e.name.textContent = (chorus && LEX_CHORUS_NAMES[k.id]) || k.name; e.val.textContent = knobText(k.id, state.knobs[k.id]); }
  lcd.textContent = `P${q.P.n} ${q.P.name.toUpperCase()}`;
  const m = preview.lex.memoryUse(), c = state.parts;
  const shrunk = !chorus && m.scale < q.P.size - 1e-3 ? ` Shrunk to ${Math.round(m.scale / q.P.size * 100)}% to fit.` : '';
  note.textContent = (chorus
    ? 'Eight voices, four a side, each delay wandering at random. The first four knobs are the voice-pair levels; Diffusion acts on pairs 3 and 4.'
    : `Predelay ${q.P.pre[0]}–${q.P.pre[1]} ms on this program.`) +
    ` ${(c.LEX_FS / 1000).toFixed(1)} kHz, ${Math.round(c.LEX_BITS)}-bit converters, ${m.words.toLocaleString()} of ${m.of.toLocaleString()} words.${shrunk}` +
    ' Program names, control ranges and converter specs follow Lexicon; the algorithms are estimates built on Dattorro\'s Griesinger-style plate. Parts are in Options under 224.';
}

// ---------- drive slot: ProCo RAT, Boss DS-1, Ibanez TS808 (fx.rat switches it on, drvtype picks the pedal) ----------
const DRV_TYPES = [
  { id: 'rat', short: 'RAT', name: 'ProCo RAT: LM308 op-amp into a diode pair, Filter after' },
  { id: 'ds', short: 'DS-1', name: 'Boss DS-1: transistor booster, op-amp into a diode pair to ground, LPF/HPF tone blend' },
  { id: 'ts', short: 'TS808', name: 'Ibanez TS808 Tube Screamer: diodes in the op-amp feedback, active tone stage' },
];
const drvTypeEl = document.getElementById('drv-type');
drvTypeEl.innerHTML = DRV_TYPES.map((T, i) => `<button type="button" role="radio" data-t="${i}" title="${T.name}">${T.short}</button>`).join('');
drvTypeEl.addEventListener('click', (e) => { const b = e.target.closest('button[data-t]'); if (b) setKnob('drvtype', b.dataset.t / 2); });
// a function declaration with no top-level consts, so drawKnob can call it before this file has run
function drawDrv() {
  const t = Math.max(0, Math.min(2, Math.round((state.knobs.drvtype || 0) * 2))), id = ['rat', 'ds', 'ts'][t];
  for (const b of document.getElementById('drv-type').children) b.setAttribute('aria-checked', +b.dataset.t === t);
  for (const k of KNOBS) if (k.drv && knobEls[k.id]) knobEls[k.id].wrap.hidden = k.drv !== id;
}
drawDrv();

// ---------- effects ----------
const FX_UNITS = ['fold', 'rat', 'dly', 'lex'];
function renderFx() {
  for (const n of FX_UNITS) {
    const b = document.getElementById('fx-' + n + '-on');
    b.setAttribute('aria-pressed', !!state.fx[n]); b.textContent = state.fx[n] ? 'On' : 'Off';
    document.getElementById('fx-' + n).classList.toggle('on', !!state.fx[n]);
  }
  document.getElementById('fx-cmp').classList.toggle('on', !!(state.fx.lex || state.fx.dly)); // the Compactor acts on the returns
  drawDlyMode(); drawDrv();
}
// Delay mode: ping-pong or mono (stored as the dlypp knob)
const dlyModeEl = document.getElementById('dly-mode');
function drawDlyMode() {
  const pp = state.knobs.dlypp >= 0.5 ? 1 : 0; for (const b of dlyModeEl.children) b.setAttribute('aria-checked', +b.dataset.m === pp);
}
dlyModeEl.addEventListener('click', (e) => { const b = e.target.closest('button[data-m]'); if (b) { setKnob('dlypp', +b.dataset.m); drawDlyMode(); } });
for (const n of FX_UNITS) document.getElementById('fx-' + n + '-on').onclick = () => {
  state.fx[n] = !state.fx[n]; preview.setFx(n, state.fx[n]); send({ type: 'fx', name: n, value: state.fx[n] });
  renderFx(); saveSession();
};

