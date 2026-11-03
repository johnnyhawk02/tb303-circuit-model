// ---------- projects: everything at once (sound, effects, tempo, all 48 bank slots, chain) ----------
const SLOT_RE = /^[A-D]([1-9]|1[0-2])$/;
function projectSnap() {
  storeSlot();
  return { sound: soundSnap(), bpm: state.bpm, sel: { ...sel }, chain: [...chain],
    banks: Object.fromEntries(Object.entries(banks).map(([k, v]) => [k, JSON.parse(JSON.stringify(v))])) };
}
// a preset is a whole project, or (folded in from the old separate lists) just a sound or just a pattern
const sndOnlyEl = document.getElementById('prj-sndonly');
sndOnlyEl.checked = !!store.get('prjSoundOnly', false);
sndOnlyEl.onchange = () => store.set('prjSoundOnly', sndOnlyEl.checked);
function applyPreset(x) {
  if (!x || typeof x !== 'object') return;
  if (x.kind === 'pattern') { if (!sndOnlyEl.checked) { applyPattern(x.pattern); storeSlot(); renderBank(); saveSession(); } return; }
  if (x.kind === 'sound' || sndOnlyEl.checked) { applySound(x.sound || {}); return; }
  applyProject(x);
}
function applyProject(x) {
  if (!x || typeof x !== 'object') return;
  if (x.sound) applySound(x.sound); else applySound({});
  if (typeof x.bpm === 'number') { state.bpm = Math.min(200, Math.max(60, Math.round(x.bpm))); drawBpm(); send({ type: 'bpm', value: state.bpm }); }
  if (x.banks && typeof x.banks === 'object') {
    for (const k of Object.keys(banks)) delete banks[k];
    for (const [k, v] of Object.entries(x.banks)) {
      if (!SLOT_RE.test(k)) continue;
      const c = cleanSettings(v); if (!c.pattern) continue;
      banks[k] = { shuffle: c.shuffle ?? 0, gate: c.gate ?? 0.5, pattern: c.pattern, ...(v.auto ? { auto: true, name: String(v.name || '') } : {}) };
    }
    store.set('banks', banks);
  }
  if (x.sel && 'ABCD'.includes(x.sel.g) && x.sel.n >= 0 && x.sel.n < 12) sel = { g: x.sel.g, n: x.sel.n | 0 };
  store.set('bankSel', sel);
  chain = Array.isArray(x.chain) ? x.chain.filter(k => SLOT_RE.test(k)) : [];
  store.set('chain', chain); playingSlot = '';
  const c = cleanSettings(banks[slotKey(sel.g, sel.n)]);
  state.pattern = c.pattern || blankPattern(); state.shuffle = c.shuffle ?? 0; state.gate = c.gate ?? 0.5;
  tail = []; sendGroove(); renderSeq(); pushPattern(); renderBank(); saveSession();
}
const PROJECT_BUILTIN = { 'Init (stock sound, empty banks)': { sound: {}, bpm: 128, banks: {}, chain: [] } };
for (const [n, x] of Object.entries(SOUND_BUILTIN)) PROJECT_BUILTIN['Sounds / ' + n] = { kind: 'sound', sound: x };
for (const [n, x] of Object.entries(PATTERN_BUILTIN)) PROJECT_BUILTIN['Patterns / ' + n] = { kind: 'pattern', pattern: x };
// the old sound and pattern bars stay hidden in the page so Options' Export all / Import keep their lists
const soundBar = presetBar('snd', 'soundPresets', SOUND_BUILTIN, soundSnap, applySound);
const patBar = presetBar('pat', 'patternPresets', PATTERN_BUILTIN, patternSnap, applyPattern);
const prjBar = presetBar('prj', 'projectPresets', PROJECT_BUILTIN, projectSnap, applyPreset, (taken) => NAMER(taken));
// fold saved sound and pattern presets into the one list (once, and again for anything imported later)
function foldPresets() {
  const done = new Set(store.get('folded', [])); let n = 0;
  const fold = (src, kind) => { for (const [name, x] of Object.entries(src)) {
    const id = kind + ':' + name; if (done.has(id)) continue; done.add(id);
    let nm = name; for (let i = 2; nm in prjBar.user; i++) nm = `${name.slice(0, 34)} (${i})`;
    prjBar.user[nm] = { kind, [kind]: x }; n++;
  } };
  fold(soundBar.user, 'sound'); fold(patBar.user, 'pattern');
  store.set('folded', [...done]); if (n) { prjBar.save(); prjBar.render(''); }
}
foldPresets();
const download = (name, obj) => {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([JSON.stringify(obj, null, 2)], { type: 'application/json' }));
  a.download = name; document.body.appendChild(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(a.href), 1000);
};
document.getElementById('prj-export').onclick = () => {
  const nm = (document.getElementById('prj-name').value.trim() || 'tb303-preset').replace(/[^\w .-]+/g, '_');
  download(nm + '.json', { app: 'tb303-circuit-model', kind: 'project', version: 1, project: projectSnap() });
  prjBar.say(`Exported ${nm}.json.`);
};
const prjFile = document.getElementById('prj-file');
document.getElementById('prj-import').onclick = () => prjFile.click();
prjFile.addEventListener('change', async () => {
  const f = prjFile.files[0]; prjFile.value = ''; if (!f) return;
  try {
    const j = JSON.parse(await f.text()), x = j?.project || j?.current?.project;
    if (!x || typeof x !== 'object') throw 0;
    applyProject(x);
    const nm = f.name.replace(/\.json$/i, '').trim().slice(0, 40) || 'Imported';
    prjBar.user[nm] = projectSnap(); prjBar.save(); prjBar.render('u:' + nm);
    prjBar.say(`Loaded and saved as ${nm}.`);
  } catch { prjBar.say('That file is not a preset export from this page.'); }
});
for (const el of document.querySelectorAll('.pm')) presetMenu(el);
document.getElementById('prj-roll').onclick = () => prjBar.suggest(true);
document.querySelector('#prj-bar .pm-btn')?.addEventListener('click', () => prjBar.suggest(false));

document.getElementById('preset-export').onclick = () => {
  const data = JSON.stringify({ app: 'tb303-circuit-model', version: 2,
    current: { sound: soundSnap(), pattern: patternSnap() },
    soundPresets: soundBar.user, patternPresets: patBar.user, projectPresets: prjBar.user }, null, 2);
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([data], { type: 'application/json' }));
  a.download = 'tb303-settings.json'; document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  prjBar.say(`Exported tb303-settings.json: current sound and pattern, ${Object.keys(soundBar.user).length} sound and ${Object.keys(patBar.user).length} pattern presets.`);
};
const fileEl = document.getElementById('preset-file');
document.getElementById('preset-import').onclick = () => fileEl.click();
fileEl.addEventListener('change', async () => {
  const f = fileEl.files[0]; fileEl.value = ''; if (!f) return;
  try {
    const j = JSON.parse(await f.text());
    if (!j || typeof j !== 'object') throw 0;
    if (j.version === 3) unmigrateTree(j);
    let ns = 0, np = 0;
    const add = (src, bar, pick) => { if (src && typeof src === 'object') for (const [n, x] of Object.entries(src)) { const nm = n.trim().slice(0, 40); if (nm) { bar.user[nm] = pick(cleanSettings(x)); bar === soundBar ? ns++ : np++; } } };
    add(j.soundPresets, soundBar, c => ({ knobs: c.knobs, parts: c.parts, wave: c.wave, fx: c.fx }));
    add(j.patternPresets, patBar, c => ({ bpm: c.bpm, shuffle: c.shuffle, gate: c.gate, pattern: c.pattern }));
    if (j.projectPresets && typeof j.projectPresets === 'object') { for (const [n, x] of Object.entries(j.projectPresets)) { const nm = n.trim().slice(0, 40); if (nm && x && typeof x === 'object') prjBar.user[nm] = x; } prjBar.save(); prjBar.render(''); }
    if (j.presets) { // version 1 files: combined presets
      add(j.presets, soundBar, c => ({ knobs: c.knobs, parts: c.parts, wave: c.wave }));
      const withPat = Object.fromEntries(Object.entries(j.presets).filter(([, x]) => Array.isArray(x?.pattern)));
      add(withPat, patBar, c => ({ bpm: c.bpm, shuffle: c.shuffle, gate: c.gate, pattern: c.pattern }));
    }
    soundBar.save(); patBar.save(); soundBar.render(''); patBar.render(''); foldPresets();
    const cur = j.current;
    if (cur?.sound || cur?.pattern) { if (cur.sound) applySound(cur.sound); if (cur.pattern) applyPattern(cur.pattern); }
    else if (cur) { applySound(cur); applyPattern(cur); }
    prjBar.say(`Imported ${ns} sound and ${np} pattern preset${np === 1 ? '' : 's'}${cur ? ', and applied the saved sound and pattern' : ''}.`);
  } catch { prjBar.say('That file is not a settings export from this page. Use Export all here to see the format.'); }
});

for (const k of KNOBS) { preview.setKnob(k.id, state.knobs[k.id]); drawKnob(k.id); }
for (const k in COMPONENTS) preview.setComponent(k, state.parts[k]);
preview.waveform = state.wave; drawBpm();
drawWave();
for (const n in state.fx) preview.setFx(n, state.fx[n]);
