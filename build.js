// Builds index.html, the whole app in one file (so it works offline and from any static host), from src/:
//   src/index.html     page markup, with /*@css*/, /*@model*/, /*@worklet*/ and /*@app*/ slots
//   src/css/*.css      styles, in CSS_ORDER
//   src/js/app/*.js    the page script, in APP_ORDER, run inside one function scope
//   src/js/worklet.js  the AudioWorklet processor
//   tb303-model.js     the voice model (also used by test.js in Node)
//   patterns.json      factory patterns, raw material for Generate
const fs = require('fs'), path = require('path');
const src = (...p) => fs.readFileSync(path.join(__dirname, ...p), 'utf8');
const CSS_ORDER = ['base', 'top', 'panel', 'sequencer', 'options', 'responsive', 'lcd'];
const APP_ORDER = ['state', 'generate', 'format', 'knobs', 'sequencer', 'banks', 'functions', 'parts', 'options', 'audio', 'voice', 'fx', 'namer', 'presets', 'projects', 'midi', 'main', 'fit'];
const css = CSS_ORDER.map(n => src('src/css', n + '.css').trimEnd()).join('\n\n');
const app = APP_ORDER.map(n => src('src/js/app', n + '.js').trimEnd()).join('\n\n')
  .replace('/*@patterns*/{}', () => src('patterns.json').trim());
const fill = { css, model: src('tb303-model.js').trimEnd(), worklet: src('src/js/worklet.js').trimEnd(), app };
const html = src('src/index.html').replace(/\/\*@(css|model|worklet|app)\*\//g, (_, k) => fill[k]);
for (const k of ['css', 'model', 'worklet', 'app']) if (!src('src/index.html').includes(`/*@${k}*/`)) throw new Error('missing slot ' + k);
fs.writeFileSync(path.join(__dirname, 'index.html'), html);
console.log(`index.html: ${(html.length / 1024).toFixed(0)} kB`);
