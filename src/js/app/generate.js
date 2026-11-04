// the ABL3 and Phoscyon factory patterns, from patterns.json; a step is code * 8 + gate + 2 * accent + 4 * slide
const PATTERN_BANK = /*@patterns*/{};
// they are not listed as presets: Generate uses them as raw material only
const SOURCES = [];
for (const [g, ps] of Object.entries(PATTERN_BANK)) for (const [n, [, steps]] of Object.entries(ps))
  if (steps.some(v => v & 1)) SOURCES.push({ name: `${g.replace(/^ABL3 /, '')} / ${n}`, pattern: steps.map(v => ({ code: v >> 3, gate: !!(v & 1), accent: !!(v & 2), slide: !!(v & 4) })) });
const rint = (a, b) => a + Math.floor(Math.random() * (b - a + 1));
// Generate: a random source pattern, fitted to the slot length, then one to three of these moves
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
const densOf = (P) => P.filter(st => st.gate).length / P.length;
// density: null keeps the source's own; otherwise a fraction of steps that play. Sources near it are preferred, then notes are added or removed to hit it
function generatePattern(len, dens = null) {
  let pool = SOURCES;
  if (dens != null) pool = [...SOURCES].sort((a, b) => Math.abs(densOf(a.pattern) - dens) - Math.abs(densOf(b.pattern) - dens)).slice(0, 60);
  const src = pool[Math.floor(Math.random() * pool.length)], sp = src.pattern;
  // build one bar (or the whole source if it is longer), move it, then later bars are the first one with one more move each
  const barLen = Math.min(len, Math.max(16, sp.length));
  const P = Array.from({ length: barLen }, (_, i) => ({ ...sp[i % sp.length] }));
  const did = Object.keys(GEN_MOVES).sort(() => Math.random() - 0.5).slice(0, rint(1, 3)).map(m => GEN_MOVES[m](P));
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
  return { pattern: out, src: src.name, did };
}

