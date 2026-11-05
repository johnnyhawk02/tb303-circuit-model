// ---------- inside the VST3 plug-in ----------
// The plug-in runs the same engine in C++ (plugin/engine) and plays it from the DAW's transport and tempo, so there is no
// Play button or Tempo dial (hidden in lcd.css). This page sends the engine what initAudio sends in a browser, and takes
// the playing step, the DAW tempo and MIDI CC back.
if (tbPlugin) {
  tbPlugin.on((m) => {
    if (!m) return;
    if (m.type === 'step') markStep(m.step);
    else if (m.type === 'bpm') { if (typeof m.value === 'number' && isFinite(m.value)) { state.bpm = Math.round(m.value * 100) / 100; bpmVal.textContent = state.bpm; } }
    else if (m.type === 'midi' && Array.isArray(m.data)) onMidi({ data: m.data });
  });
  for (const k of KNOBS) send({ type: 'knob', name: k.id, value: state.knobs[k.id] });
  for (const k in state.parts) if (state.parts[k] !== COMPONENTS[k].v) send({ type: 'component', name: k, value: state.parts[k] });
  send({ type: 'wave', value: state.wave }); sendTune(); sendPattern(); sendGroove();
  for (const n in state.fx) send({ type: 'fx', name: n, value: state.fx[n] });
  sendDeclick();
  send({ type: 'ready' });
}
