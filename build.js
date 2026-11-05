// Builds index.html, the whole app in one file (so it works offline and from any static host), from src/:
//   src/index.html     page markup, with /*@css*/, /*@model*/, /*@worklet*/ and /*@app*/ slots
//   src/css/*.css      styles, in CSS_ORDER
//   src/js/app/*.js    the page script, in APP_ORDER, run inside one function scope
//   src/js/worklet.js  the AudioWorklet processor
//   tb303-model.js     the voice model (also used by test.js in Node)
//   patterns.json      factory patterns, raw material for Generate
//   factory-presets.json  the Factory presets in the Preset menu (whole projects, as Export saves them)
const fs = require('fs'), path = require('path');
const src = (...p) => fs.readFileSync(path.join(__dirname, ...p), 'utf8');
const CSS_ORDER = ['base', 'top', 'panel', 'sequencer', 'options', 'responsive', 'lcd'];
const APP_ORDER = ['state', 'generate', 'format', 'knobs', 'sequencer', 'banks', 'functions', 'seqmenu', 'parts', 'options', 'audio', 'voice', 'fx', 'tuning', 'namer', 'presets', 'projects', 'midi', 'main', 'fit', 'statusline', 'plugin'];
const css = CSS_ORDER.map(n => src('src/css', n + '.css').trimEnd()).join('\n\n');
const app = APP_ORDER.map(n => src('src/js/app', n + '.js').trimEnd()).join('\n\n')
  .replace('/*@patterns*/{}', () => src('patterns.json').trim())
  .replace('/*@factory*/{}', () => src('factory-presets.json').trim());
const fill = { css, model: src('tb303-model.js').trimEnd(), worklet: src('src/js/worklet.js').trimEnd(), app };
const html = src('src/index.html').replace(/\/\*@(css|model|worklet|app)\*\//g, (_, k) => fill[k]);
if (app.includes('/*@factory*/')) throw new Error('factory presets slot not filled');
for (const k of ['css', 'model', 'worklet', 'app']) if (!src('src/index.html').includes(`/*@${k}*/`)) throw new Error('missing slot ' + k);
const web = html.replace('<!--@plugin-->\n', '');
fs.writeFileSync(path.join(__dirname, 'index.html'), web);
console.log(`index.html: ${(web.length / 1024).toFixed(0)} kB`);
// the plug-in's page (plugin/): the same app with plugin/web/shim.js first (the plug-in puts its saved state in at
// <!--@boot-->), the VT323 font served by the plug-in instead of Google Fonts, and no web-app icons or manifest
const ui = html
  .replace('<!--@plugin-->\n', () => `<!--@boot-->\n<script>\n${src('plugin/web/shim.js').trim()}\n</script>\n`)
  .replace(/<link rel="(manifest|icon|apple-touch-icon|preconnect)"[^>]*>\n/g, '')
  .replace(/<link rel="stylesheet" href="https:\/\/fonts\.googleapis\.com[^>]*>\n/, '<style>@font-face { font-family: "VT323"; src: url("VT323-Regular.ttf") format("truetype"); }</style>\n');
if (/https:\/\/fonts|<!--@plugin-->/.test(ui) || !ui.includes('<!--@boot-->')) throw new Error('plug-in page not made');
fs.writeFileSync(path.join(__dirname, 'plugin/Resources/ui.html'), ui);
console.log(`plugin/Resources/ui.html: ${(ui.length / 1024).toFixed(0)} kB`);
