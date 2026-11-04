// Compares two raw float32 renders: node compare.js a.f32 b.f32 [tolerance in dB below full scale]
const fs = require('fs');
const a = new Float32Array(fs.readFileSync(process.argv[2]).buffer.slice(0)), b = new Float32Array(fs.readFileSync(process.argv[3]).buffer.slice(0));
const tolDb = +(process.argv[4] || -60);
if (a.length !== b.length) { console.log(`length differs: ${a.length} vs ${b.length}`); process.exit(1); }
let max = 0, at = 0, se = 0, sp = 0, exact = 0;
for (let i = 0; i < a.length; i++) { const d = Math.abs(a[i] - b[i]); if (d > max) { max = d; at = i; } se += d * d; sp += a[i] * a[i]; if (d === 0) exact++; }
const db = (x) => (20 * Math.log10(x)).toFixed(1);
console.log(`frames ${a.length / 2}, signal rms ${db(Math.sqrt(sp / a.length))} dBFS, max diff ${db(max)} dBFS at ${(at / 2 / 48000).toFixed(3)} s, diff rms ${db(Math.sqrt(se / a.length))} dBFS, identical samples ${(100 * exact / a.length).toFixed(1)}%`);
process.exit(20 * Math.log10(max) <= tolDb ? 0 : 1);
