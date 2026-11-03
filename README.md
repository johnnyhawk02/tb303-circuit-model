# TB-303 circuit model

A JavaScript simulation of the TB-303 bass voice, modelled at component level from the Roland main board schematic (Feb 1982). Values that sheet doesn't show clearly were taken from the Behringer TD-3 schematic.

Open `index.html` in a browser to play it. It has a step sequencer with pattern presets, the front panel knobs, a scope, and a parts list you can edit while it plays.

## Files
- `tb303-model.js`: the voice (`TB303Voice`) and its parts table (`COMPONENTS`). It has no dependencies and runs in Node, in a browser, or in an AudioWorklet.
- `tb303-model.js` also has `TB303Engine`, the player around the voice (knob slew, de-click, the step sequencer). The AudioWorklet and the main-thread fallback both run it.
- `src/`: the page source. `src/index.html` is the markup with slots; `src/css/` holds the styles by area, `src/js/worklet.js` the AudioWorklet wrapper, and `src/js/app/` the app by feature (sequencer, banks, knobs, parts, board, presets, MIDI and so on).
- `build.js`: `node build.js` fills the slots in `src/index.html` with the CSS, the model, the worklet, the app code and `patterns.json`, and writes the single-file `index.html` that gets deployed. Edit `src/`, never `index.html`.
- `test.js`: sanity checks (DAC voltages, 110 Hz at the A key, filter range, decay times) and renders `demo.wav`.
- `patterns.json`: the ABL3 and Phoscyon factory patterns behind the pattern presets. `tools/import-patterns.js` makes it from the plug-ins' files, and `node build.js` puts it in the page.
- `calib.js`: works out the ladder's self-oscillation gain (k = 17) and its resonance-frequency factor.

## What's modelled
- **Pitch CV:** a 6-bit R-2R ladder (100k/200k) from 5.333 V, which gives 1/12 V per step.
- **Slide:** an RC lag through R93 and C35.
- **VCO:** an exponential current charges C33 from 5.5 V to 12 V, then resets. The reset is smoothed with a polyBLEP.
- **Square:** the ramp is high-passed through C10 into the Q8 PNP switch.
- **VCF:** the 4-pole transistor/diode ladder with an 18n bottom cap and 33n caps above it, and a tanh law for each transistor pair. It is solved with an implicit trapezoidal step at 2× oversampling. Resonance feedback is high-passed by C15.
- **Envelopes:** the filter envelope charges C62 and decays through R138 + VR6 (200 ms to 2.5 s). On accented notes VR6 is shorted and the accent sweep cap (1µ/47k) charges.
- **VCA:** an OTA fed through C49/R88. The resonance pot wiper is also mixed in through C76/R100 to compensate for lost bass.
- **Noise:** white voltage noise at the ladder input (EN_VCF), the OTA input (EN_OTA, the main hiss, which opens and closes with the VCA) and the output stage (EN_OUT, a constant floor). NOISE scales them all from 0 (silent) to 0.15 (the default: hiss around -80 dBFS under notes, -91 dBFS idle).

Each part's `source` field is `schematic`, `td3` or `estimate`.

## Effects
After the VCA there is an optional effect, modelled from its parts:
- **ProCo RAT:** an LM308 non-inverting stage limited by gain-bandwidth and slew rate (set by the 30p compensation cap). It then has 1N914 clipping diodes to ground, solved with Newton's method each sample, and the 1.5k + 100k / 3.3n filter. The usual mods (diode swaps, the Ruetz leg, gain-leg caps, the compensation cap) are marked in the RAT parts in Options.
- **Lexicon 224 (1978, David Griesinger):** a stereo reverb after the RAT. It runs on its own 20 kHz clock behind 6-pole anti-alias and reconstruction filters, with 12-bit gain-ranging converters (+24 dB), a zero-order-hold DAC, an input transformer, and 16-bit words in one shared 32K-word circular delay memory. Programs that don't fit are shrunk, as on the hardware. It has the nine v4.4 programs and the 224's controls: Bass and Mid decay (0.6–70 s) split at Crossover, Treble decay, Depth (0–71), Diffusion (0–63), each program's Predelay range, and Mode Enhancement (random tap modulation, Amount and Pitch). In Chorus A, eight voices wander at random and the first four knobs set the voice-pair levels.
  - **What is documented:** the program names, control ranges, sample rate, converters, word size and memory (the Lexicon v4.4 manual, the spec sheet, and Dattorro 1997, footnote 5).
  - **What is estimated:** Lexicon never published the 224's microcode. The core is the "plate-class topology in the style of Griesinger" that Jon Dattorro published (*Effect Design Part 1*, JAES 1997), using his delay lengths and output taps: input allpass diffusers feed a figure-eight tank. The hall, chamber, room and constant-density variants, the Depth mapping, the Mode Enhancement step timing, the chorus voice timings and the truncation rule are my estimates. The converter and memory settings are in Options under 224.
- **Ducker:** a sidechain limiter on the 224 return, keyed from the dry synth, so the reverb drops back while the synth plays and swells in the gaps. A peak follower (1 ms attack, Release 20 ms to 1 s) turns the wet down by however far the synth is over Threshold (-40 to 0 dBFS), by no more than Duck (0 to 30 dB; 0 is off). It is a mixing tool, not part of either unit.

The RAT, the ducker and the 224 share the FX tab.

