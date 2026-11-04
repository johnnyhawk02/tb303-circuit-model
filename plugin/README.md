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
- **Everything the web app has, laid out the same way.** Voice knobs with the web page's readouts, the
  TM3 filter trim, and *New unit* / *Messed up* / *Undo* for the components. Tabs for the Sequencer, the
  twelve Mods (E24 / E12 steps, Shift for in-between), FX (RAT, Delay with Ping-pong / Mono, Compactor with
  Amount and Release, the reverb with its 224 / Plate / Spring / Conv switch, program buttons, LCD and note), Circuit (every part by block),
  Board (the stylised PCB), Monitor (de-click, scope, readouts), MIDI (the CC map) and About.
- **Sequencer.** Banks A-D x slots 1-12, chains (Cmd-click or right-click a slot), Copy / Paste / Clear /
  Refill, patterns up to 64 steps (the grid shows at least 32; clicking past the end extends), Length,
  Duplicate steps, Shuffle notes, Step len, Shift, Transpose, Generate with Density and Bars, and the
  Functions menu (Join chain and copy, Alter pattern, Undo). It follows the host's transport and tempo,
  locked to the bar; there is no Play button or tempo dial, the host does that.
- **MIDI.** Notes play the voice at any time: mono, last note wins, a note played while another is held
  slides (legato), velocity 100 or more is an accent. MIDI A2 (note 45) is the 303's A, 110 Hz. CC 1-19
  move the front-panel dials, as in the web app.
- **Presets.** The top bar has the web app's preset browser: search, star favourites (and show only them),
  previous / next, Save with the auto-namer (and another name), Delete, and *Sound only*. Presets are whole
  projects (sound, all 48 slots and the chain), kept in
  `~/Library/Application Support/TB303 Circuit Model/presets.json` and shared by every instance. *File*
  imports the web app's files (a project, favourites or Export all) and exports a preset, your favourites,
  or everything, in the web app's format.
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
