# TB-303 circuit model

A JavaScript simulation of the TB-303 bass voice, modelled at component level from the Roland main board schematic (Feb 1982). Values that sheet doesn't show clearly were taken from the Behringer TD-3 schematic.

Open `index.html` in a browser to play it. It has a step sequencer with pattern presets, the front panel knobs, a scope, and a parts list you can edit while it plays.

## Files
- `tb303-model.js`: the voice (`TB303Voice`) and its parts table (`COMPONENTS`). It has no dependencies and runs in Node, in a browser, or in an AudioWorklet.
- `page.html`: the page template. `node build.js` injects the model and writes `index.html`.
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

Each part's `source` field is `schematic`, `td3` or `estimate`.

## Effects
After the VCA there is an optional effect, modelled from its parts:
- **ProCo RAT:** an LM308 non-inverting stage limited by gain-bandwidth and slew rate (set by the 30p compensation cap). It then has 1N914 clipping diodes to ground, solved with Newton's method each sample, and the 1.5k + 100k / 3.3n filter. The usual mods (diode swaps, the Ruetz leg, gain-leg caps, the compensation cap) are marked in the RAT parts tab.
