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
    // Cmd+C / Cmd+V / Cmd+Z, which the DAW's Edit menu would otherwise take: replayed as the keydown the page listens for
    else if (m.type === 'key' && /^[a-z]$/.test(m.key)) {
      (document.activeElement || document.body).dispatchEvent(new KeyboardEvent('keydown', { key: m.key, code: 'Key' + m.key.toUpperCase(), metaKey: true, shiftKey: !!m.shift, bubbles: true, cancelable: true }));
    }
  });
  // while a text field has focus, Cmd+C / Cmd+V are the field's own
  const textField = (t) => !!t?.closest?.('input[type=text], input[type=number], input[type=search], input:not([type]), textarea, [contenteditable=""], [contenteditable=true]');
  document.addEventListener('focusin', (e) => send({ type: 'typing', on: textField(e.target) }));
  document.addEventListener('focusout', () => send({ type: 'typing', on: false }));
  for (const k of KNOBS) send({ type: 'knob', name: k.id, value: state.knobs[k.id] });
  for (const k in state.parts) if (state.parts[k] !== COMPONENTS[k].v) send({ type: 'component', name: k, value: state.parts[k] });
  send({ type: 'wave', value: state.wave }); sendTune(); sendPattern(); sendGroove();
  for (const n in state.fx) send({ type: 'fx', name: n, value: state.fx[n] });
  sendDeclick();
  send({ type: 'ready' });
}
