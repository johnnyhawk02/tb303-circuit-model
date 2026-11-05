// ---------- the VST3 / AU plug-in (plugin/) ----------
// The plug-in shows this same page (plugin/Resources/ui.html, written by build.js) and runs the C++ port of the engine.
// The host plays, stops and sets the tempo, so there is no Play button or Tempo dial; everything else is the web app.
// plugin/web/shim.js keeps localStorage in the plug-in (this project's keys with the DAW project, the rest shared)
// and passes messages both ways: send() goes to the C++ engine, and the engine answers with the playing step,
// the host tempo, and dials moved by host automation or MIDI CC.
if (PLUGIN) {
  const B = window.TB303_BRIDGE;
  document.documentElement.classList.add('plugin');
  // what initAudio() sends when the web app starts its audio: the engine gets the whole sound and pattern
  for (const k of KNOBS) send({ type: 'knob', name: k.id, value: state.knobs[k.id] });
  for (const k in state.parts) send({ type: 'component', name: k, value: state.parts[k] });
  send({ type: 'wave', value: state.wave }); sendTune(); sendPattern(); sendGroove();
  for (const n in state.fx) send({ type: 'fx', name: n, value: state.fx[n] });
  sendDeclick();
  B.on('step', (m) => markStep(m.step));
  B.on('rate', (m) => { document.getElementById('rate').textContent = `2× of ${m.value / 1000} kHz`; });
  B.on('bpm', (m) => { state.bpm = m.value; }); // the host's tempo (presets and the session keep it as the web app does)
  // a dial the host moved (automation, MIDI CC through the host)
  B.on('set', (m) => {
    if (m.what === 'knob' && m.id in state.knobs) setKnob(m.id, m.value);
    else if (m.what === 'part' && m.id in COMPONENTS) applyPart(m.id, m.value);
    else if (m.what === 'wave') { state.wave = m.value; preview.waveform = m.value; drawWave(); saveSession(); }
    else if (m.what === 'fx' && m.id in state.fx) { state.fx[m.id] = !!m.value; preview.setFx(m.id, state.fx[m.id]); renderFx(); saveSession(); }
  });
  document.getElementById('midi-status').textContent = 'MIDI comes from the host: notes play the voice (legato slides, velocity 100 and up accents), CCs move the dials.';
  B.send({ type: 'ready' });
}
