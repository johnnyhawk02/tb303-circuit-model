# TB303 Circuit Model plug-in

The web app's TB-303 model as a VST3 / AU instrument (and a standalone app), built with JUCE 8.

## What's in it
- **The same engine.** `Source/dsp/TB303Model.h` is a line-by-line C++ port of `../tb303-model.js`: the
  component-level voice, circuit noise, the ProCo RAT, the delay, the Compactor and the reverb slot (Lexicon 224, Dattorro plate, spring tank, convolution with the irs/ impulse responses embedded), plus
  the knob slew, de-click and step sequencer. `tests/` renders two scenarios (patterns, knob moves, every
  effect, a waveform switch, 48 and 44.1 kHz) with both and compares them. On Linux nearly every sample is
  identical; the C++ flushes denormals to zero like the plug-in, which moves a few tiny values (around
  -140 dBFS). CI fails above -120 dBFS, so a change to `tb303-model.js` that isn't ported shows up.
- **Tables generated from the JS.** `Source/dsp/Tables.h` (parts and knobs) and `Source/PartsMeta.h` (part notes)
  are written by `node plugin/tools/gen-tables.js` from `COMPONENTS` in `tb303-model.js` and `KNOBS` in
  `src/js/app/state.js`. Run it after changing either; CI fails if they are stale. New knobs go at the end of
  its `KNOB_ORDER`, which is the plug-in's parameter order.
- **The web app's own screen.** The window is the web app's page itself: `Resources/ui.html`, which
  `node build.js` writes next to `index.html` from the same `src/` (so commit both after any `src/` change; CI
  checks). It runs in the system web view and is served from the plug-in, with the VT323 font embedded, so the
  look, the pages, the preset menu with all the factory presets, the sequencer, the FX page, Options and every
  key command are the web app's. The only differences: no Play button and no Tempo dial, because the host
  plays, stops and sets the tempo, and no Install app button.
- **How the page drives the engine.** `src/js/app/plugin.js` and `web/shim.js`: every message the page would
  send the web app's AudioWorklet (`knob`, `component`, `wave`, `fx`, `tune`, `pattern`, `groove`, `declick`)
  goes to the processor instead, which sends back the playing step, the host tempo and any dial moved by
  automation or MIDI CC. The page's localStorage lives in the plug-in: this project's keys (the session,
  pattern slots, chain, open tab) are saved with the host project; presets, favourites and the screen look
  are shared by every instance in `~/Library/Application Support/TB303 Circuit Model/web-store.json`
  (presets saved by the earlier plug-in are moved into My presets on first run). Export all / Export
  favourites open a save panel.
- **Host parameters.** The voice and FX dials, the front-panel mods, the waveform and the FX switches are
  automatable; moving one in the host moves it on the page.
- **Sequencer.** It follows the host's transport and tempo, locked to the bar: step *n* of the pattern (or
  chain) plays on 16th note *n*.
- **MIDI.** Notes play the voice at any time: mono, last note wins, a note played while another is held
  slides (legato), velocity 100 or more is an accent. MIDI A2 (note 45) is the 303's A, 110 Hz. CC 1-8
  move the voice dials in panel order (Tuning, Cutoff, Reso, Key track, Env mod, Decay, Accent, Volume),
  CC 9 the Filter trim and CC 10-21 the Mods page, as in the web app.
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
runs the parity test (needs Node) and the host test (transport sync, MIDI, the page's messages and the saved
state through the processor). On Linux the window loads the page from a temporary folder, because JUCE's
WebKitGTK helper can't take the page over its pipe; macOS and Windows serve it straight from the plug-in.
