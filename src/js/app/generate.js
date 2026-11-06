// Generate's own pattern maker: no stored patterns, every line is built from a scale, a rhythm cell and a few acid habits
// (root-heavy lines, octave jumps tied with a slide, accents off the beat), then varied bar by bar with the moves below.
// a step is { code, gate, accent, slide }; code 0-59 is the 303's note number (33 = A)
const GEN_SCALES = {
  minor: [0, 2, 3, 5, 7, 8, 10], phrygian: [0, 1, 3, 5, 7, 8, 10], dorian: [0, 2, 3, 5, 7, 9, 10],
  'minor pentatonic': [0, 3, 5, 7, 10], blues: [0, 3, 5, 6, 7, 10], 'harmonic minor': [0, 2, 3, 5, 7, 8, 11],
};
const GEN_STYLES = ['Rolling', 'Stab', 'Squelch', 'Walker', 'Octave', 'Sparse'];
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const pick = (a) => a[Math.floor(Math.random() * a.length)];
// one bar: a 4- or 8-step rhythm cell repeated with small changes, notes from the scale around the root
function makeBar(len, o) {
  const { root, scale, style, dens } = o;
  const cellLen = style === 'Rolling' || style === 'Octave' ? 4 : pick([4, 8, 8]);
  // the chance a step plays: downbeats most, then 8ths, then 16ths; Stab and Sparse lean on fewer, stronger hits
  const w = (i) => (i % 4 === 0 ? 1.25 : i % 2 === 0 ? 0.95 : 0.8) * (style === 'Sparse' ? 0.6 : style === 'Stab' && i % 2 ? 0.55 : 1);
  const cell = Array.from({ length: cellLen }, (_, i) => Math.random() < Math.min(0.97, dens * w(i)));
  if (!cell.some(Boolean)) cell[0] = true;
  // a short motif of scale degrees, mostly the root, walking or leaping by style
  const deg = (d) => root + Math.floor(d / scale.length) * 12 + scale[((d % scale.length) + scale.length) % scale.length];
  let d = 0;
  const motif = Array.from({ length: cellLen }, (_, i) => {
    if (i === 0 || Math.random() < (style === 'Walker' ? 0.15 : 0.4)) d = 0;
    else d += style === 'Walker' ? pick([-1, 1, 1, 2]) : pick([-2, -1, 1, 2, 3, 4]);
    d = Math.max(-scale.length, Math.min(scale.length * 2 - 1, d));
    let c = deg(d);
    if (style === 'Octave' ? i % 2 === 1 : Math.random() < 0.18) c += Math.random() < 0.8 ? 12 : -12;
    return c;
  });
  const P = [];
  for (let i = 0; i < len; i++) {
    const k = i % cellLen, rep = Math.floor(i / cellLen);
    let gate = cell[k], code = motif[k];
    // later repeats change a step now and then, so the bar answers itself instead of looping
    if (rep > 0 && Math.random() < 0.18) gate = !gate;
    if (rep > 0 && Math.random() < 0.22) code = deg(pick([0, 0, 1, 2, -1, 3, 4])) + (Math.random() < 0.2 ? 12 : 0);
    if (i === 0) gate = true;
    const off = i % 4 !== 0;
    const accent = gate && Math.random() < (style === 'Stab' ? 0.4 : off ? 0.26 : 0.14);
    P.push({ code, gate, accent, slide: false });
  }
  // slides tie a note into the next played one: most often across an octave jump or into a step one down the scale
  for (let i = 0; i < len - 1; i++) {
    if (!P[i].gate || !P[i + 1].gate) continue;
    const jump = Math.abs(P[i + 1].code - P[i].code);
    if (Math.random() < (style === 'Squelch' ? 0.35 : jump >= 12 ? 0.3 : 0.12)) P[i].slide = true;
  }
  for (const st of P) st.code = Math.max(0, Math.min(59, st.code));
  return P;
}
const rint = (a, b) => a + Math.floor(Math.random() * (b - a + 1));
// moves applied to a made bar, and once to each later bar so the phrase develops
const GEN_MOVES = {
  // grab a few steps and loop them to the end
  loop(P) { const n = P.length, k = rint(2, Math.max(2, Math.min(8, n >> 1))), at = rint(0, n - k), from = rint(at + k, n - 1);
    const seg = P.slice(at, at + k); for (let i = from; i < n; i++) P[i] = { ...seg[(i - from) % k] }; return `looped steps ${at + 1}–${at + k} from step ${from + 1} to the end`; },
  reverse(P) { if (Math.random() < 0.5) { P.reverse(); return 'reversed'; }
    const h = P.length >> 1, back = P.slice(h).reverse(); P.splice(h, back.length, ...back); return 'reversed the second half'; },
  // a step plays twice in a row, pushing the rest along (the end drops off)
  dup(P) { const c = rint(1, Math.max(1, P.length >> 3)); for (let j = 0; j < c; j++) { const i = rint(0, P.length - 2); P.splice(i, 0, { ...P[i] }); P.pop(); } return `doubled ${c} step${c > 1 ? 's' : ''}`; },
  shuffle(P) { const n = P.length, b = n >= 8 && Math.random() < 0.5 ? 4 : 1, blocks = [];
    for (let i = 0; i < n; i += b) blocks.push(P.slice(i, i + b));
    const sw = rint(1, Math.max(1, blocks.length >> 2));
    for (let j = 0; j < sw; j++) { const x = rint(0, blocks.length - 1), y = rint(0, blocks.length - 1); [blocks[x], blocks[y]] = [blocks[y], blocks[x]]; }
    P.splice(0, n, ...blocks.flat()); return b > 1 ? `swapped ${sw} beat${sw > 1 ? 's' : ''}` : `swapped ${sw} step${sw > 1 ? 's' : ''}`; },
};
// density: null keeps the source's own; otherwise a fraction of steps that play. Sources near it are preferred, then notes are added or removed to hit it
function generatePattern(len, dens = null) {
  const style = pick(GEN_STYLES), sn = pick(Object.keys(GEN_SCALES)), root = rint(24, 38);
  const o = { root, scale: GEN_SCALES[sn], style, dens: dens != null ? Math.max(0.2, Math.min(0.95, dens * 1.1)) : 0.45 + Math.random() * 0.4 };
  const name = `${style} ${NOTE_NAMES[root % 12]} ${sn}`;
  // build one bar, move it, then later bars are the first one with one more move each
  const barLen = Math.min(len, 16);
  const P = makeBar(barLen, o);
  const did = Object.keys(GEN_MOVES).sort(() => Math.random() - 0.5).slice(0, rint(0, 2)).map(m => GEN_MOVES[m](P));
  const out = [...P];
  while (out.length < len) {
    const seg = P.map(st => ({ ...st })), mv = Object.keys(GEN_MOVES); GEN_MOVES[mv[rint(0, mv.length - 1)]](seg);
    out.push(...seg.slice(0, len - out.length));
  }
  if (len > barLen) did.push(`bars 2–${Math.ceil(len / 16)} varied`);
  if (dens != null) {
    const want = Math.max(1, Math.round(dens * len)), pitches = out.filter(st => st.gate).map(st => st.code);
    const idx = (g) => [...out.keys()].filter(i => out[i].gate === g).sort(() => Math.random() - 0.5);
    let on = out.filter(st => st.gate).length;
    // add notes on off-beats last, remove them from off-beats first, so the downbeats survive
    const beat = (i) => (i % 4 === 0 ? 0 : i % 2 === 0 ? 1 : 2);
    if (on < want) for (const i of idx(false).sort((a, b) => beat(a) - beat(b))) { if (on >= want) break; out[i].gate = true; out[i].code = pitches.length ? pitches[rint(0, pitches.length - 1)] : out[i].code; out[i].slide = false; on++; }
    if (on > want) for (const i of idx(true).sort((a, b) => beat(b) - beat(a))) { if (on <= want) break; if (i === 0) continue; out[i].gate = false; out[i].accent = false; out[i].slide = false; on--; }
  }
  if (!out.some(st => st.gate)) out[0].gate = true;
  return { pattern: out, src: name, did };
}

