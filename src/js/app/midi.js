// ---------- MIDI: CC 1 onwards, any channel ----------
// CC 1-8 are the main panel's dials left to right (Key track sits after Reso), CC 9 the Filter trim, then the Mods page.
const voiceCc = (k) => ({ name: k.name, set: (t) => setKnob(k.id, t) });
const MIDI_MAP = [...KNOBS.filter(k => !k.fx).flatMap(k => k.id === 'resonance'
    ? [voiceCc(k), { name: 'Key track', set: (t) => setPartT('KT', t, true) }] : [voiceCc(k)]),
  { name: 'Filter trim', set: (t) => setPartT('I_REF', t) },
  ...FRONT_MODS.map(([k, name]) => ({ name, set: (t) => setPartT(k, t) }))];
// Notes (any channel, octaves named as in Logic and Ableton, C3 = middle C = note 60): C0-G0 (24-31) pick pattern 1-8,
// C1-C3 (36-60) set the root note the pattern plays from, C1 as written, each semitone up shifts it up one. The root
// note is a performance setting: it is not saved and comes back to C1 when the page loads.
const NOTE_PAT = 24, NOTE_ROOT = 36, ROOT_MAX = 24;
let midiTranspose = 0;
function sendTranspose() { send({ type: 'transpose', value: midiTranspose }); }
document.getElementById('midi-map').innerHTML = MIDI_MAP.map((m, i) => `<dt>CC ${i + 1}</dt><dd>${m.name}</dd>`).join('')
  + '<dt>C0-G0</dt><dd>Pattern 1-8</dd><dt>C1-C3</dt><dd>Root note (C1 = as written)</dd>';
const midiPending = new Map(); let midiFrame = 0;
function onMidiNote(note) {
  if (note >= NOTE_PAT && note < NOTE_PAT + 8) { const n = note - NOTE_PAT; if (n !== sel.n) loadSlot(sel.g, n); }
  else if (note >= NOTE_ROOT && note <= NOTE_ROOT + ROOT_MAX && note - NOTE_ROOT !== midiTranspose) { midiTranspose = note - NOTE_ROOT; sendTranspose(); }
}
function onMidi(e) {
  const [st, cc, val] = e.data;
  if ((st & 0xf0) === 0x90 && val > 0) { onMidiNote(cc); return; }
  if ((st & 0xf0) !== 0xb0 || cc < 1 || cc > MIDI_MAP.length) return;
  midiPending.set(cc, val / 127);
  if (!midiFrame) midiFrame = requestAnimationFrame(() => { midiFrame = 0; for (const [c, t] of midiPending) MIDI_MAP[c - 1].set(t); midiPending.clear(); });
}
if (tbPlugin) document.getElementById('midi-status').textContent = 'In the plug-in, MIDI comes from the DAW track, all channels. C3 = middle C.';
else if (navigator.requestMIDIAccess) {
  const midiStatus = document.getElementById('midi-status');
  navigator.requestMIDIAccess().then((acc) => {
    const hook = () => { const ins = [...acc.inputs.values()]; for (const i of ins) i.onmidimessage = onMidi;
      midiStatus.textContent = ins.length ? `Listening on ${ins.map(i => i.name).join(', ')}, all channels. C3 = middle C.` : 'No MIDI input connected. Plug one in and it is picked up straight away.'; };
    hook(); acc.onstatechange = hook;
  }, () => { midiStatus.textContent = 'MIDI access was refused or is not available here.'; });
} else document.getElementById('midi-status').textContent = 'This browser has no Web MIDI. Chrome and Edge have it.';

