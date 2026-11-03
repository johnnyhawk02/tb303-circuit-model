# TB303 Circuit Model plug-in

The web app's TB-303 model as a VST3 / AU instrument (and a standalone app), built with JUCE 8.

## What's in it
- **The same engine.** `Source/dsp/TB303Model.h` is a line-by-line C++ port of `../tb303-model.js`: the
  component-level voice, circuit noise, the ProCo RAT, the delay, the Compactor and the Lexicon 224, plus
  the knob slew, de-click and step sequencer. `tests/` renders one scenario (pattern, knob moves, every
  effect, a waveform switch) with both and compares them; on Linux they are identical to the last bit.
  CI runs this on every push, so a change to `tb303-model.js` that isn't ported shows up as a failure.
- **Panel.** Voice knobs with the web page's readouts, the twelve front-panel mods and the TM3 filter trim
  (all automatable), the FX page, and the piano-roll sequencer (up to 32 steps, length, shift, transpose).
- **Sequencer.** With *Sequencer* on, the pattern follows the host's transport, locked to its bar position.
  *Play* runs it on its own clock (the *Tempo* dial) while the host is stopped, as in the standalone app.
- **MIDI.** Notes play the voice at any time: mono, last note wins, a note played while another is held
  slides (legato), velocity 100 or more is an accent. MIDI A2 (note 45) is the 303's A, 110 Hz.
- **Presets.** *Import* reads the web app's files: a project export, the favourites file, or Export all.
  Favourites land in the plug-in's Presets menu (kept in
  `~/Library/Application Support/TB303 Circuit Model/presets.json`, shared by every instance).
  *Export* writes a project file the web app can import. Parts that aren't front-panel mods (anything you
  changed on the web page's Board or parts list) come across with the preset and are saved with the session.
- **De-click** adds 3 ms of latency, reported to the host so it compensates.

## Build
CI (`.github/workflows/plugin.yml`) builds a universal macOS binary, signs it ad hoc, runs pluginval and
auval, and uploads `TB303-Circuit-Model-macOS` as an artifact (see `INSTALL.txt` inside it).

Locally:
```
cmake -S plugin -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
JUCE is fetched by CMake; pass `-DJUCE_DIR=/path/to/JUCE` to use a checkout. `ctest --test-dir build`
runs the parity test (needs Node) and the host test (transport sync and MIDI through the processor).
