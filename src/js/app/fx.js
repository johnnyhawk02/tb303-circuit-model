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

// ---------- Lexicon 224 ----------
{
  const el = document.getElementById('lex-progs');
  el.innerHTML = LEX_PROGRAMS.map((P, i) => `<button type="button" role="radio" data-i="${i}" aria-checked="false" title="${P.name}"><b>${P.n}</b>${P.short}</button>`).join('');
  el.addEventListener('click', (e) => { const b = e.target.closest('button'); if (b) setKnob('lexprog', b.dataset.i / 8); });
}
const LEX_CHORUS_NAMES = { lexbass: 'Pair 1', lexmid: 'Pair 2', lexxover: 'Pair 3', lextreble: 'Pair 4', lexdepth: 'Wander' };
function drawLex() {
  const progs = document.getElementById('lex-progs'); if (!progs || !knobEls.lexmix) return;
  const q = lexParams(state.knobs), chorus = q.P.alg === 'chorus';
  for (const b of progs.children) b.setAttribute('aria-checked', +b.dataset.i === q.prog);
  for (const k of KNOBS) if (k.fx === 'lex') { const e = knobEls[k.id]; e.name.textContent = (chorus && LEX_CHORUS_NAMES[k.id]) || k.name; e.val.textContent = knobText(k.id, state.knobs[k.id]); }
  document.getElementById('lex-lcd').textContent = `P${q.P.n} ${q.P.name.toUpperCase()}`;
  const m = preview.lex.memoryUse(), c = state.parts;
  const shrunk = !chorus && m.scale < q.P.size - 1e-3 ? ` Shrunk to ${Math.round(m.scale / q.P.size * 100)}% to fit.` : '';
  document.getElementById('lex-note').textContent = (chorus
    ? 'Eight voices, four a side, each delay wandering at random. The first four knobs are the voice-pair levels; Diffusion acts on pairs 3 and 4.'
    : `Predelay ${q.P.pre[0]}–${q.P.pre[1]} ms on this program.`) +
    ` ${(c.LEX_FS / 1000).toFixed(1)} kHz, ${Math.round(c.LEX_BITS)}-bit converters, ${m.words.toLocaleString()} of ${m.of.toLocaleString()} words.${shrunk}` +
    ' Program names, control ranges and converter specs follow Lexicon; the algorithms are estimates built on Dattorro\'s Griesinger-style plate. Parts are in Options under 224.';
}

// ---------- effects ----------
function renderFx() {
  for (const n of ['rat', 'lex']) {
    const b = document.getElementById('fx-' + n + '-on');
    b.setAttribute('aria-pressed', state.fx[n]); b.textContent = state.fx[n] ? 'On' : 'Off';
    document.getElementById('fx-' + n).classList.toggle('on', state.fx[n]);
  }
}
for (const n of ['rat', 'lex']) document.getElementById('fx-' + n + '-on').onclick = () => {
  state.fx[n] = !state.fx[n]; preview.setFx(n, state.fx[n]); send({ type: 'fx', name: n, value: state.fx[n] });
  renderFx(); saveSession();
};

