// Reads AudioRealism ABL3 .pat files and D16 Phoscyon 2 .ph2seq files into patterns.json, which build.js puts in the page.
//   node tools/import-patterns.js <ABL3 Patterns folder> <unpacked Phoscyon sequencer patterns folder>
// The Phoscyon folder is the zip inside the zip "Presets/Sequencer Patterns.d16pkg".
// patterns.json: { group: { name: [bpm or 0, [step, ...]] } }, a step being code * 8 + gate + 2 * accent + 4 * slide.
// Both plug-ins put the low C of the keyboard at note 36 (c-3), which is DAC code 24, so code = note - 12.
const fs = require('fs'), path = require('path');
const [ablRoot, phRoot] = process.argv.slice(2);
if (!ablRoot || !phRoot) { console.error('usage: node tools/import-patterns.js <abl3 folder> <phoscyon folder>'); process.exit(2); }
const NAMES = { c: 0, d: 2, e: 4, f: 5, g: 7, a: 9, b: 11 };
const noteOf = (t) => { const m = /^([a-g])([-#b])(\d+)$/i.exec(t); return m ? NAMES[m[1].toLowerCase()] + (m[2] === '#' ? 1 : m[2].toLowerCase() === 'b' ? -1 : 0) + 12 * +m[3] : null; };
// `c-3 gate accent slide`, or `semitone down up slide accent gate` (column names from the one XML patch's labels)
function row(f) {
  if (f.length === 6 && f.every((x) => /^-?\d+$/.test(x))) { const [p, down, up, slide, accent, gate] = f.map(Number); return { note: 36 + p + 12 * (up - down), gate, accent, slide }; }
  if (f.length === 4 && noteOf(f[0]) !== null) return { note: noteOf(f[0]), gate: +f[1], accent: +f[2], slide: +f[3] };
  return null;
}
function textPatterns(text) {
  const out = []; let cur = null;
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim(), tag = /Meta tag:\s*(\d+)/.exec(line), tempo = /Tempo:\s*([\d.]+)/.exec(line);
    if (tag) { cur = { len: +tag[1], rows: [], bpm: 0 }; out.push(cur); continue; }
    if (tempo && cur) cur.bpm = Math.round(+tempo[1]);
    if (!line || line.startsWith(';')) continue;
    const r = row(line.split(/\s+/));
    if (!r) continue;
    if (!cur) { cur = { len: 0, rows: [], bpm: 0 }; out.push(cur); }
    cur.rows.push(r);
  }
  return out.map((p) => ({ bpm: p.bpm, rows: p.len ? p.rows.slice(0, p.len) : p.rows }));
}
function xmlPattern(text) {
  const val = (k) => { const m = new RegExp(`property="${k}"[^>]*>\\s*(-?[\\d.]+)`).exec(text); return m ? Number(m[1]) : 0; };
  return [{ bpm: 0, rows: Array.from({ length: val('dpatternlength') || 16 }, (_, i) => ({ note: 36 + val(`dpitch${i}`) + 12 * (val(`dup${i}`) - val(`ddown${i}`)), gate: val(`dgate${i}`), accent: val(`daccent${i}`), slide: val(`dslide${i}`) })) }];
}
// steps="33a|338|...": three hex digits a step, the octave, the semitone, then flags (8 gate, 2 accent, 1 slide). D16 don't
// document the flags. 2 acts like the accent column of the ABL3 files (falls on the beat, rarely in runs), 1 like slide.
function stepsPattern(text) {
  const m = /steps="([0-9a-f|]+)"/i.exec(text);
  return m ? [{ bpm: 0, rows: m[1].split('|').map((x) => { const v = parseInt(x, 16); return { note: (v >> 8) * 12 + ((v >> 4) & 15), gate: (v >> 3) & 1, accent: (v >> 1) & 1, slide: v & 1 }; }) }] : [];
}
function encode(rows) {
  return rows.map((r) => {
    let code = r.note - 12;
    while (code > 57) code -= 12;
    while (code < 9) code += 12;
    return r.gate ? code * 8 + 1 + (r.accent ? 2 : 0) + (r.slide ? 4 : 0) : code * 8;
  });
}
const walk = (d) => fs.readdirSync(d).sort().flatMap((f) => { const p = path.join(d, f); return fs.statSync(p).isDirectory() ? walk(p) : [p]; });
const bank = {}, seen = new Set();
let kept = 0, skipped = 0;
function add(group, name, { bpm, rows }) {
  const steps = encode(rows), key = steps.join();
  if (steps.length < 4 || steps.length > 64 || rows.filter((r) => r.gate).length < 2 || seen.has(key)) { skipped++; return; }
  seen.add(key); kept++;
  const g = bank[group] ||= {};
  let n = name, i = 2;
  while (n in g) n = `${name} ${i++}`;
  g[n] = [bpm >= 60 && bpm <= 200 ? bpm : 0, steps];
}
for (const f of walk(ablRoot)) {
  if (!/\.pat$/i.test(f)) continue;
  const text = fs.readFileSync(f, 'latin1'), group = 'ABL3 ' + path.relative(ablRoot, f).split(path.sep)[0];
  for (const p of text.includes('<JukeboxPatch') ? xmlPattern(text) : textPatterns(text)) add(group, path.basename(f, path.extname(f)), p);
}
for (const f of walk(phRoot)) {
  if (!/\.ph2seq$/i.test(f)) continue;
  for (const p of stepsPattern(fs.readFileSync(f, 'latin1'))) add('Phoscyon', path.basename(f, path.extname(f)), p);
}
const lines = Object.entries(bank).map(([g, ps]) => `${JSON.stringify(g)}:{\n${Object.entries(ps).map(([n, p]) => `${JSON.stringify(n)}:${JSON.stringify(p)}`).join(',\n')}\n}`);
fs.writeFileSync(path.join(__dirname, '..', 'patterns.json'), `{\n${lines.join(',\n')}\n}\n`);
console.log(`${kept} patterns kept, ${skipped} empty, too long or repeated`);
