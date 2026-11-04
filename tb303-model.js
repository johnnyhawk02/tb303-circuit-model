/*
 * TB-303 component-level voice model
 * -----------------------------------
 * Built from the Roland TB-303 main board schematic (Feb 19, 1982).
 *
 * Every block below is driven by the part values in COMPONENTS. Change a
 * resistor or capacitor and the derived coefficients (time constants, cutoff
 * scaling, ramp slope...) are recomputed by voice.setComponent().
 *
 * Signal path (as on the schematic):
 *   IC9 4174 latch + R74-R90 R-2R ladder (5.333 V ref)  -> pitch CV
 *   IC12 4066 + R93/C35 RC lag                          -> slide
 *   Q26 2SC1583 expo converter + C33 integrator,
 *   Q27/Q28 reset at 12 V                               -> saw 5.5..12 V
 *   C10/R_SQB high-pass + Q8 PNP switch                   -> square 5..8 V
 *   Q12 diff pair + Q13-Q23 transistor/diode ladder,
 *   C18 (18n) C19/C24/C26 (33n), Q10/Q11 expo current   -> 4-pole VCF
 *   Q21/Q19 output amp, VR4 + C15 feedback (resonance)
 *   Q38/Q40 env: C62 charged, decays via R138 + VR6     -> VCF envelope
 *   VR7 + D27/R117 + accent cap ("sweep")               -> accent
 *   IC15 BA662A OTA, Q36 / C72 env                      -> VCA
 *
 * The voice runs at 2x oversampling. The VCF is integrated with an implicit
 * trapezoidal step, with each transistor pair's tanh() linearised around the
 * previous state, so it stays stable at any cutoff.
 *
 * Works in Node, in a browser, and inside an AudioWorklet (no DOM use).
 */

const VT = 0.02585; // thermal voltage at ~300 K

// source: 'schematic' = value printed on the 1982 sheet
//         'rat'       = standard ProCo RAT values
//         'lex'       = Lexicon 224 documentation or published specs
//         'td3'       = read from the Behringer TD-3 (clone) schematic
//         'estimate'  = not legible / not printed, chosen to match the
//                       behaviour the sheet documents (ramp levels, decay times)
const COMPONENTS = {
  // --- Pitch CV: IC9 4174 + R74..R90 matched R-2R ladder, 0.1% selected
  VREF:   { v: 5.333,   unit: 'V', block: 'CV',    source: 'schematic', note: 'IC9/IC10 supply; 5.333/64 = 1/12 V per step', range: [4, 6.5, 'lin'] },
  R_DAC:  { v: 100e3,   unit: 'Ω', block: 'CV',    source: 'schematic', note: 'R74-R90 series arms' },
  R2_DAC: { v: 200e3,   unit: 'Ω', block: 'CV',    source: 'schematic', note: 'R78 etc. 200K legs' },

  // --- Slide: IC12 4066 switch around RC lag
  R93:    { v: 100e3,   unit: 'Ω', block: 'Slide', source: 'schematic', note: 'slide resistor', mod: 'Slide time. The Devil Fish puts a pot here; bigger values give a lazier glide.' },
  C35:    { v: 0.22e-6, unit: 'F', block: 'Slide', source: 'schematic', note: 'slide cap (.22)', mod: 'Slide time cap. Swapping it is the simple slide-time mod; 1µ gives a long, smeary glide.' },

  // --- VCO
  C33:    { v: 0.01e-6, unit: 'F', block: 'VCO',   source: 'schematic', note: 'integrator timing cap' },
  V_LO:   { v: 5.5,     unit: 'V', block: 'VCO',   source: 'schematic', note: 'ramp bottom (scope trace)', range: [3, 8, 'lin'] },
  V_HI:   { v: 12.0,    unit: 'V', block: 'VCO',   source: 'schematic', note: 'ramp top / reset threshold (scope trace)', range: [9, 15, 'lin'] },
  F_TUNE: { v: 110,     unit: 'Hz',block: 'VCO',   source: 'schematic', note: 'TM4: 110 Hz at CV 2.75 V (A key)', range: [55, 220, 'log'] },
  CV_TUNE:{ v: 2.75,    unit: 'V', block: 'VCO',   source: 'schematic', note: 'CV out at A key', range: [1, 4, 'lin'] },
  VR2:    { v: 50e3,    unit: 'Ω', block: 'VCO',   source: 'schematic', note: 'TUNING 50K(B), ±1 oct assumed', fixed: true },

  // --- Square shaper (Q8 2SA733)
  C10:    { v: 0.01e-6, unit: 'F', block: 'Square',source: 'schematic', note: 'coupling from saw' },
  R_SQB:  { v: 100e3,   unit: 'Ω', block: 'Square',source: 'td3',       note: 'Q8 base bias; HPF with C10 sets pitch-dependent duty (TD-3 R191)' },
  SQ_LO:  { v: 5.0,     unit: 'V', block: 'Square',source: 'schematic', note: 'square low (scope trace)' },
  SQ_HI:  { v: 8.0,     unit: 'V', block: 'Square',source: 'schematic', note: 'square high (scope trace)' },
  R_SQC:  { v: 10e3,    unit: 'Ω', block: 'Square',source: 'td3',       note: 'Q8 collector load (TD-3 R175)' },
  R_SQE:  { v: 1e3,     unit: 'Ω', block: 'Square',source: 'estimate',  note: 'Q8 emitter degeneration' },

  // --- VCF ladder
  C18:    { v: 0.018e-6,unit: 'F', block: 'VCF',   source: 'schematic', note: 'bottom stage cap (.018)', mod: 'Bottom ladder cap. Changing it toward 33n alters how the resonance sits; a popular experiment, not a standard mod.' },
  C19:    { v: 0.033e-6,unit: 'F', block: 'VCF',   source: 'schematic', note: 'stage 2 cap (.033)' },
  C24:    { v: 0.033e-6,unit: 'F', block: 'VCF',   source: 'schematic', note: 'stage 3 cap (.033)' },
  C26:    { v: 0.033e-6,unit: 'F', block: 'VCF',   source: 'schematic', note: 'top stage cap (.033)' },
  R_IN:   { v: 100e3,   unit: 'Ω', block: 'VCF',   source: 'td3',       note: 'waveform to Q12 base (TD-3 R27)', mod: 'Filter input level. Lowering it drives the ladder harder for grit, like the Devil Fish overdrive.' },
  R98:    { v: 2.2e3,   unit: 'Ω', block: 'VCF',   source: 'schematic', note: 'Q12 base to ground (input divider)' },
  C15:    { v: 1e-6,    unit: 'F', block: 'VCF',   source: 'schematic', note: 'resonance feedback coupling (1/50)' },
  VR4:    { v: 50e3,    unit: 'Ω', block: 'VCF',   source: 'schematic', note: 'RESONANCE 50K(B)', fixed: true },
  A_Q21:  { v: 16.5,    unit: '×', block: 'VCF',   source: 'estimate',  note: 'Q21/Q19 loop gain at VR4 max (k_crit = 17)', range: [0, 26, 'lin'], mod: 'Resonance range. The Devil Fish and many TD-3 mods raise feedback so the filter self-oscillates (k above 17 here).' },
  // expo converter Q10/Q11 summing node
  R73:    { v: 100e3,   unit: 'Ω', block: 'VCF',   source: 'schematic', note: 'cutoff pot to Q10 base' },
  R72:    { v: 220e3,   unit: 'Ω', block: 'VCF',   source: 'schematic', note: 'env mod to Q10 base', mod: 'Env mod depth. Lowering it is the usual more-envelope mod for bigger sweeps.' },
  R63:    { v: 100e3,   unit: 'Ω', block: 'VCF',   source: 'schematic', note: 'accent to Q10 base' },
  R71:    { v: 2.2e3,   unit: 'Ω', block: 'VCF',   source: 'schematic', note: 'Q10 base to ground' },
  V_CUT:  { v: 3.3,     unit: 'V', block: 'VCF',   source: 'estimate',  note: 'VR3 wiper span (R47 10K end stop)', range: [0.5, 6, 'lin'], mod: 'Cutoff knob range. Cutoff range mods (common on TD-3s) let the knob go lower and higher.' },
  KT:     { v: 0,       unit: '%', block: 'VCF',   source: 'estimate',  note: 'KEY TRACK pot (100K B) with click-off switch: buffered pitch CV minus V_KT, through R_KT into Q10 base. Not on a stock 303', range: [0, 1, 'lin'], mod: 'Filter key tracking, as on the TD-3-MO and Devil Fish. Feeds the pitch CV into the cutoff so higher notes open the filter. With the 36K R_KT about a third of the pot is exact 1 V/oct and full is about 300%, so low notes almost vanish, like a TD-3-MO turned up. 0 switches it out, which is the stock 303.' },
  R_KT:   { v: 36e3,    unit: 'Ω', block: 'VCF',   source: 'estimate',  note: 'key track pot wiper to Q10 base (114K = 100% at full pot, 36K about 300%)', range: [18e3, 300e3, 'log'], mod: 'Key track range. Lower values let the Key track pot go further past 1 V/oct.' },
  V_KT:   { v: 3.0,     unit: 'V', block: 'VCF',   source: 'estimate',  note: 'key track buffer reference: the note whose cutoff does not move (3 V = the C above the A tuning note); notes below it get darker', range: [1, 4, 'lin'] },
  I_REF:  { v: 3.1e-6, unit: 'A', block: 'VCF',   source: 'estimate',  note: 'TM3 FREQ trim: ladder current at 0 V', range: [1.55e-6, 6.2e-6, 'log'], mod: 'Filter frequency trimmer (TM3). Sets where the cutoff sits for a given knob position, about one octave either way.' },

  // --- VCF envelope (Q38/Q40/Q39 2SK30)
  C62:    { v: 1e-6,    unit: 'F', block: 'Env',   source: 'schematic', note: 'envelope cap (1/35)', mod: 'Envelope cap. Scales every decay time together; a smaller cap gives snappier envelopes.' },
  R138:   { v: 68e3,    unit: 'Ω', block: 'Env',   source: 'schematic', note: 'decay min resistor', mod: 'Sets the shortest decay and the accent decay. The Devil Fish adds separate Normal and Accent decay pots, reaching much shorter decays.' },
  VR6:    { v: 1e6,     unit: 'Ω', block: 'Env',   source: 'schematic', note: 'ENV.DECAY 1M(A): 200 ms .. 2.5 s', mod: 'Decay pot. Raising it (or adding a series pot) gives longer decays; it is the Normal decay control on a Devil Fish.' },
  R152:   { v: 100,     unit: 'Ω', block: 'Env',   source: 'td3',       note: 'attack charge path (TD-3 R125 = 100)' },
  V_ENV:  { v: 5.0,     unit: 'V', block: 'Env',   source: 'estimate',  note: 'envelope peak' },
  VR5:    { v: 50e3,    unit: 'Ω', block: 'Env',   source: 'schematic', note: 'ENV.MOD 50K(A)', fixed: true },

  // --- Accent
  VR7:    { v: 50e3,    unit: 'Ω', block: 'Accent',source: 'schematic', note: 'ACCENT 50K(B)', fixed: true },
  R117:   { v: 22e3,    unit: 'Ω', block: 'Accent',source: 'schematic', note: 'charge resistor into sweep cap', mod: 'Accent charge resistor. Lower values give a harder, faster accent sweep.' },
  C_ACC:  { v: 1e-6,    unit: 'F', block: 'Accent',source: 'schematic', note: 'accent sweep cap C13 (1/50); TD-3 C62 1µ', mod: 'Accent sweep cap. The Devil Fish Sweep Speed control changes how fast this charges and bleeds, so stacked accents wah more or less.' },
  R_ACC:  { v: 47e3,    unit: 'Ω', block: 'Accent',source: 'schematic', note: 'sweep resistor R46 47K; TD-3 R39 47K', mod: 'Accent sweep bleed. Part of the Sweep Speed mod: higher values let consecutive accents build further.' },
  V_D:    { v: 0.55,    unit: 'V', block: 'Accent',source: 'estimate',  note: 'D27/D28 forward drop' },
  WOW:    { v: 0,       unit: '×', block: 'Accent',source: 'estimate',  note: 'blend toward the schematic accent sweep: D24 / R_ACC / second gang of VR4 / C_ACC, gang wiper to the filter', range: [0, 1, 'lin'], mod: 'Accent wow. Blends in the sweep as the cleaner schematic draws it: the second gang of the resonance pot taps the sweep cap, so at high resonance accents swell and build over back-to-back accents. 0 keeps the classic accent.' },

  // --- VCA (IC15 BA662A OTA)
  C67:    { v: 1e-6,    unit: 'F', block: 'VCA',   source: 'td3',       note: 'VCA env cap (TD-3 C67 1/35)', mod: 'VCA envelope cap; scales VCA decay and attack together.' },
  R49:    { v: 100,     unit: 'Ω', block: 'VCA',   source: 'td3',       note: 'VCA env attack (TD-3 R49)', mod: 'VCA attack. Related to the Devil Fish Soft Attack control, which slows the attack of non-accented notes.' },
  R_SOFT: { v: 3.3e3,   unit: 'Ω', block: 'VCA',   source: 'estimate',  note: 'soft attack on non-accented notes (about 3 ms with C67)', mod: 'Soft attack. The Devil Fish Soft Attack control varies this; stock 303 normal notes fade in over a few ms, accented notes bypass it and snap in. Lower it for more click, raise it for a slower swell.' },
  R52:    { v: 1.5e6,   unit: 'Ω', block: 'VCA',   source: 'td3',       note: 'VCA env decay (TD-3 R52 1.5M)', mod: 'VCA (VEG) decay. The Devil Fish VEG Decay control: shorter for gated plucks, longer for drones.' },
  R_VREL: { v: 4.7e3,   unit: 'Ω', block: 'VCA',   source: 'estimate',  note: 'gate-off discharge through T28' },
  V_VCA:  { v: 5.0,     unit: 'V', block: 'VCA',   source: 'estimate',  note: 'VCA env peak' },
  R105:   { v: 22e3,    unit: 'Ω', block: 'VCA',   source: 'td3',       note: 'env to OTA bias pin (Iabc) (TD-3 R105)' },
  R25:    { v: 22e3,    unit: 'Ω', block: 'VCA',   source: 'td3',       note: 'accent to VCA, with D8 (TD-3 R25)' },
  R26:    { v: 47e3,    unit: 'Ω', block: 'VCA',   source: 'td3',       note: 'accent to VCA (TD-3 R26, C26 .033)' },
  A_OUT:  { v: 10,      unit: '×', block: 'VCA',   source: 'estimate',  note: 'Q21 diff stage gain (R83/R84 22K)' },
  C14:    { v: 1e-6,    unit: 'F', block: 'VCA',   source: 'schematic', note: 'VCF output coupling (1/50)' },
  R85:    { v: 10e3,    unit: 'Ω', block: 'VCA',   source: 'td3',       note: 'output buffer load, HPF with C14 (TD-3 R85)' },
  C49:    { v: 0.01e-6, unit: 'F', block: 'VCA',   source: 'td3',       note: 'filter to VCA coupling (TD-3 C49)' },
  R88:    { v: 220e3,   unit: 'Ω', block: 'VCA',   source: 'td3',       note: 'filter to VCA input (TD-3 R88)' },
  C76:    { v: 0.01e-6, unit: 'F', block: 'VCA',   source: 'td3',       note: 'resonance compensation coupling (TD-3 C76)' },
  R100:   { v: 100e3,   unit: 'Ω', block: 'VCA',   source: 'td3',       note: 'resonance compensation into VCA (TD-3 R100)' },
  R101:   { v: 2.2e3,   unit: 'Ω', block: 'VCA',   source: 'td3',       note: 'OTA input to ground (TD-3 R101)' },
  R_OTA:  { v: 22e3,    unit: 'Ω', block: 'VCA',   source: 'estimate',  note: 'OTA output load' },
  C38:    { v: 1e-6,    unit: 'F', block: 'VCA',   source: 'schematic', note: 'VCA output coupling (1/50)' },
  R_OUT:  { v: 10e3,    unit: 'Ω', block: 'VCA',   source: 'estimate',  note: 'output load, HPF with C38' },

  // --- Noise: the parts' own hiss, as white voltage noise densities at the points where it enters
  EN_VCF: { v: 15e-9,   unit: 'V/√Hz', block: 'Noise', source: 'estimate', note: 'ladder input noise: R98 2.2K thermal plus Q12 pair shot noise, referred to the Q12 base. Resonance rings it, but it sits far below the OTA hiss', range: [0, 150e-9, 'lin'] },
  EN_OTA: { v: 40e-9,   unit: 'V/√Hz', block: 'Noise', source: 'estimate', note: 'BA662 OTA input noise with R101 2.2K: the main hiss, which opens and closes with the VCA', range: [0, 400e-9, 'lin'] },
  EN_OUT: { v: 0.35e-6,    unit: 'V/√Hz', block: 'Noise', source: 'estimate', note: 'output stage floor (IC15 buffer, mixer): a constant hiss under everything, after the volume pot', range: [0, 3.5e-6, 'lin'] },
  NOISE:  { v: 0.15,    unit: '×', block: 'Noise', source: 'estimate', note: 'scales every noise source together (0 = silent background)', range: [0, 0.15, 'lin'], mod: 'Circuit noise. The hiss of the old transistors, OTA and output stage; 0.15 is a light hiss, 0 is studio-clean.' },

  // --- ProCo RAT
  RAT_CIN: { v: 22e-9,  unit: 'F', block: 'RAT',   source: 'rat', note: 'input coupling into 1M' },
  RAT_RD:  { v: 100e3,  unit: 'Ω', block: 'RAT',   source: 'rat', note: 'DISTORTION pot 100k(A) in the LM308 feedback' },
  RAT_CF:  { v: 100e-12,unit: 'F', block: 'RAT',   source: 'rat', note: 'cap across the distortion pot', mod: 'Raising it smooths the fizz at high gain.' },
  RAT_RG1: { v: 47,     unit: 'Ω', block: 'RAT',   source: 'rat', note: 'gain leg 1 resistor (with 2.2µ)' },
  RAT_CG1: { v: 2.2e-6, unit: 'F', block: 'RAT',   source: 'rat', note: 'gain leg 1 cap' },
  RAT_RG2: { v: 560,    unit: 'Ω', block: 'RAT',   source: 'rat', note: 'gain leg 2 resistor (with 4.7µ)', mod: 'Ruetz mod: add resistance here (or a pot) to tame the low-mid gain.' },
  RAT_CG2: { v: 4.7e-6, unit: 'F', block: 'RAT',   source: 'rat', note: 'gain leg 2 cap', mod: 'Smaller caps cut bass before clipping for a tighter sound.' },
  RAT_CC:  { v: 30e-12, unit: 'F', block: 'RAT',   source: 'rat', note: 'LM308 compensation cap; sets slew rate and bandwidth', mod: 'The LM308 slew limit is the RAT\'s character. Smaller cap = brighter; swapping in a faster op-amp loses it.' },
  RAT_GBW: { v: 1e6,    unit: 'Hz',block: 'RAT',   source: 'estimate', note: 'LM308 gain-bandwidth at 30p', range: [2e5, 5e6, 'log'] },
  RAT_SR:  { v: 0.3,    unit: 'V/µs', block: 'RAT', source: 'estimate', note: 'LM308 slew rate at 30p', range: [0.05, 5, 'log'] },
  RAT_RCL: { v: 1e3,    unit: 'Ω', block: 'RAT',   source: 'rat', note: 'resistor into the clipping diodes' },
  RAT_VF:  { v: 0.6,    unit: 'V', block: 'RAT',   source: 'rat', note: 'clipping diode drop at 1 mA (1N914 ≈ 0.6 V)', range: [0.2, 2.5, 'lin'], mod: 'Diode swap. Germanium about 0.3 V (softer), LEDs about 1.7 V (Turbo RAT: louder, more open), 2.5 V is close to no diodes.' },
  RAT_RF:  { v: 1.5e3,  unit: 'Ω', block: 'RAT',   source: 'rat', note: 'fixed filter resistor (in series with the 100k FILTER pot)' },
  RAT_CFLT:{ v: 3.3e-9, unit: 'F', block: 'RAT',   source: 'rat', note: 'filter cap', mod: 'Sets the FILTER range; larger caps make the filter darker overall.' },
  RAT_VS:  { v: 9,      unit: 'V', block: 'RAT',   source: 'rat', note: 'supply; op-amp swings to about ±(V/2 − 1)', range: [6, 18, 'lin'] },

  // --- Lexicon 224 (1978, David Griesinger). 'lex' = from Lexicon documentation or published specs.
  LEX_FS:   { v: 20000,  unit: 'Hz', block: '224', source: 'lex', note: 'internal sample rate (20 kHz, Dattorro 1997 and spec sheet)', range: [10000, 48000, 'log'], mod: 'The 224 ran at 20 kHz, which is why it is dark and grainy. Raise it to hear the same programs with modern bandwidth.' },
  LEX_BW:   { v: 9000,   unit: 'Hz', block: '224', source: 'estimate', note: 'anti-alias and reconstruction corner, 6-pole each side (spec: 20 Hz–8 kHz +1/−2 dB)', range: [3000, 20000, 'log'] },
  LEX_BITS: { v: 12,     unit: 'bit', block: '224', source: 'lex', note: 'converter resolution (12-bit, DAC80)', range: [6, 20, 'lin'], mod: 'The 12-bit gain-ranging converters are part of the grit. 16 or more gives a clean modern converter.' },
  LEX_RANGE:{ v: 24,     unit: 'dB', block: '224', source: 'lex', note: 'gain-ranging headroom, in 6 dB steps (12-bit + 24 dB ≈ 16-bit range)', range: [0, 42, 'lin'] },
  LEX_GREL: { v: 0.08,   unit: 's',  block: '224', source: 'estimate', note: 'gain-ranging release (how long the converter holds a coarse range)', range: [0.005, 1, 'log'] },
  LEX_WORD: { v: 16,     unit: 'bit', block: '224', source: 'lex', note: 'fixed-point word size in delay memory', range: [8, 24, 'lin'] },
  LEX_MEM:  { v: 32768,  unit: 'words', block: '224', source: 'lex', note: 'delay memory (16K at launch, doubled shortly after). Programs shrink to fit.', range: [8192, 131072, 'log'] },
  LEX_XSAT: { v: 1.6,    unit: 'V',  block: '224', source: 'estimate', note: 'input transformer saturation level', range: [0.3, 6, 'log'] },
};

function audioTaper(p) { return p <= 0 ? 0 : Math.pow(p, 2.74); } // 'A' pot, ~15% at mid

// 6-bit R-2R ladder solved by Thevenin reduction from the LSB end.
function dacVoltage(code, vref, R, R2) {
  let vth = 0, rth = R2; // termination leg to ground
  for (let k = 0; k < 6; k++) {
    const vb = (code >> k) & 1 ? vref : 0;
    vth = (vth / rth + vb / R2) / (1 / rth + 1 / R2);
    rth = (rth * R2) / (rth + R2);
    if (k < 5) rth += R;
  }
  return vth;
}

// ---------------------------------------------------------------------------
// Lexicon 224 digital reverb (1978, David Griesinger)
//
// What is documented (Lexicon 224 v4.4 manual, spec sheet, Dattorro 1997):
//   20 kHz sample rate, 20 Hz–8 kHz response, 12-bit DAC80 converters with
//   gain ranging for about 24 dB more range, 16-bit fixed-point words, 16K
//   words of delay memory at launch (soon 32K). Nine v4.4 programs (P1/P3 and
//   P2/P5 share algorithms). Controls: Bass and Mid decay (0.6–70 s) split at
//   Crossover (100 Hz–10.9 kHz), Treble decay (100 Hz–10.9 kHz), Depth (0–71,
//   close to far), Diffusion (0–63), Predelay (a per-program range), and Mode
//   Enhancement: random modulation of delay taps, Amount 1–16 (time between
//   tap updates, lower is stronger) and Pitch shift 1–16.
// What is estimated: the actual 224 microcode was never published. The core
// here is the "plate-class topology in the style of Griesinger" that Dattorro
// published (input allpass diffusers, then a figure-eight tank of two
// cross-coupled halves, each a modulated allpass, delay, decay filter,
// allpass, delay), with his delay lengths and output taps. The hall, chamber,
// room and constant-density variants, the depth mapping and the chorus voice
// timings are my estimates, shaped to the manual's descriptions.
//
// Like the hardware, all delay lines share one circular memory addressed by a
// single decrementing pointer, every write is rounded to the fixed-point word,
// and a program that does not fit in LEX_MEM is shrunk until it does.
const LEX_PROGRAMS = [
  { n: 1, name: 'Small Concert Hall B',     short: 'Small Hall B', alg: 'hallB',   size: 1.2,  pre: [24, 152] },
  { n: 2, name: 'Vocal Plate',              short: 'Vocal Plate',  alg: 'plate',   size: 1.0,  pre: [0, 107] },
  { n: 3, name: 'Large Concert Hall B',     short: 'Large Hall B', alg: 'hallB',   size: 1.85, pre: [24, 152] },
  { n: 4, name: 'Acoustic Chamber',         short: 'Chamber',      alg: 'chamber', size: 1.1,  pre: [25, 255] },
  { n: 5, name: 'Percussion Plate A',       short: 'Perc Plate',   alg: 'plate',   size: 0.75, pre: [0, 107] },
  { n: 6, name: 'Small Concert Hall A',     short: 'Small Hall A', alg: 'hallA',   size: 1.25, pre: [24, 152] },
  { n: 7, name: 'Room A',                   short: 'Room A',       alg: 'room',    size: 0.5,  pre: [24, 255] },
  { n: 8, name: 'Constant Density Plate A', short: 'CD Plate',     alg: 'cdplate', size: 1.0,  pre: [5, 185] },
  { n: 9, name: 'Chorus A',                 short: 'Chorus',       alg: 'chorus',  size: 1,    pre: [0, 253] },
];
// Dattorro's plate at 29761 Hz: tank lines [L modAP, L d1, L ap2, L d2, R modAP, R d1, R ap2, R d2]
const LEX_TANK = [672, 4453, 1800, 3720, 908, 4217, 2656, 3163];
// his output taps (Table 2): [line, position, sign]
const LEX_TAPS_L = [[5, 266, 1], [5, 2974, 1], [6, 1913, -1], [7, 1996, 1], [1, 1990, -1], [2, 187, -1], [3, 1066, -1]];
const LEX_TAPS_R = [[1, 353, 1], [1, 3627, 1], [2, 1228, -1], [3, 2673, 1], [5, 2111, -1], [6, 335, -1], [7, 121, -1]];
// algorithm shapes. Only 'plate' is Dattorro's published network; the rest are estimates.
const LEX_ALGS = {
  plate:   { ind: [142, 107, 379, 277], mul: [1, 1, 1, 1, 1, 1, 1, 1], ap3: null, dd1: 0.70, er: 0.25 },
  hallB:   { ind: [142, 107, 379, 277], mul: [1.13, 1.07, 0.93, 1.11, 1.09, 0.97, 1.12, 1.04], ap3: [1123, 1327], dd1: 0.65, er: 0.45 },
  hallA:   { ind: [233, 571], mul: [1.21, 1.03, 1.07, 0.95, 0.89, 1.11, 0.97, 1.08], ap3: [977, 1201], dd1: 0.60, er: 0.5 },
  chamber: { ind: [113, 163, 293], mul: [0.83, 0.91, 1.17, 0.88, 0.95, 0.86, 1.21, 0.93], ap3: null, dd1: 0.68, er: 0.7 },
  room:    { ind: [101, 271], mul: [1, 1, 1, 1, 1, 1, 1, 1], ap3: null, dd1: 0.62, er: 0.9 },
  cdplate: { ind: [142, 107, 379, 277, 199, 151], mul: [1, 0.97, 1, 1.03, 1, 1.02, 1, 0.98], ap3: [811, 947], dd1: 0.75, er: 0.15 },
};
const LEX_CHORUS_MS = [9, 14, 19, 25]; // base delay per voice pair (estimate)

// front-panel knobs (0..1) to the 224's own units
function lexParams(k) {
  const ex = (lo, hi, p) => lo * Math.pow(hi / lo, p);
  const prog = Math.max(0, Math.min(8, Math.round(k.lexprog * 8))), P = LEX_PROGRAMS[prog];
  return {
    prog, P,
    bass: ex(0.6, 70, k.lexbass), mid: ex(0.6, 70, k.lexmid),
    xover: ex(100, 10900, k.lexxover), treble: ex(100, 10900, k.lextreble),
    depth: Math.round(k.lexdepth * 71), diff: Math.round(k.lexdiff * 63),
    pre: P.pre[0] + (P.pre[1] - P.pre[0]) * k.lexpre,
    me: k.lexme < 0.03 ? 0 : 17 - Math.max(1, Math.min(16, Math.ceil(k.lexme * 16))),
    pitch: 1 + Math.round(k.lexpitch * 15),
    pairs: [k.lexbass, k.lexmid, k.lexxover, k.lextreble],
    wet: 2 * audioTaper(k.lexmix),
  };
}

function lexLowpass(fc, Q, fs) {
  const w = 2 * Math.PI * Math.min(fc, 0.45 * fs) / fs, al = Math.sin(w) / (2 * Q), cw = Math.cos(w), a0 = 1 + al;
  return { b0: (1 - cw) / 2 / a0, b1: (1 - cw) / a0, b2: (1 - cw) / 2 / a0, a1: -2 * cw / a0, a2: (1 - al) / a0, x1: 0, x2: 0, y1: 0, y2: 0 };
}
function lexBiq(f, x) {
  for (const s of f) {
    const y = s.b0 * x + s.b1 * s.x1 + s.b2 * s.x2 - s.a1 * s.y1 - s.a2 * s.y2;
    s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y; x = y;
  }
  return x;
}

class Lexicon224 {
  constructor(fsHost) {
    this.fsHost = fsHost;
    this.F = 0; this.memWords = 0; this.prog = -1;
    this.seed = 22477; this.L = 0; this.R = 0; this.yl = 0; this.yr = 0;
    this.ph = 0; this.prev = 0; this.hx = 0; this.hxIn = 0;
    this.envIn = 0; this.envL = 0; this.envR = 0; this.zl = 0; this.zr = 0;
  }
  _rand() { this.seed = (this.seed * 1103515245 + 12345) >>> 0; return this.seed / 4294967296; }

  derive(c, k) {
    const q = lexParams(k), F = Math.round(c.LEX_FS), fh = this.fsHost;
    const layoutChanged = F !== this.F || Math.round(c.LEX_MEM) !== this.memWords || q.prog !== this.prog;
    if (F !== this.F || c.LEX_BW !== this.bw) {
      this.F = F; this.bw = c.LEX_BW; this.ratio = F / fh;
      const fc = Math.min(c.LEX_BW, 0.45 * F), Qs = [0.5176, 0.7071, 1.9319];
      this.aa = Qs.map(Qv => lexLowpass(fc, Qv, fh));
      this.rcL = Qs.map(Qv => lexLowpass(fc, Qv, fh));
      this.rcR = Qs.map(Qv => lexLowpass(fc, Qv, fh));
    }
    this.memWords = Math.round(c.LEX_MEM); this.prog = q.prog; this.q = q;
    if (layoutChanged) this._layout();
    // converters and word size
    this.adcQ = Math.pow(2, Math.round(c.LEX_BITS) - 1);
    this.steps = Math.max(0, Math.round(c.LEX_RANGE / 6));
    this.envRel = Math.exp(-1 / (c.LEX_GREL * F));
    this.wq = Math.pow(2, Math.round(c.LEX_WORD) - 1);
    this.xsat = c.LEX_XSAT;
    this.hxA = Math.exp(-2 * Math.PI * 12 / fh); // transformer low end, estimate
    this.wet = q.wet;
    // program parameters
    this.preD = Math.max(1, Math.round(q.pre * F / 1000));
    const g1 = Math.min(0.9, 0.75 * q.diff / 35);
    this.gIn = [g1, g1, g1 * 0.833, g1 * 0.833, g1 * 0.8, g1 * 0.8];
    this.dd2 = Math.min(0.7, 0.5 * q.diff / 35);
    const half = (o) => { let s = 0; for (let i = 0; i < 4; i++) s += this.len[o + i]; return s; };
    if (this.alg !== 'chorus') {
      const hL = half(0) + (this.ap3 ? this.ap3[0] : 0), hR = half(4) + (this.ap3 ? this.ap3[1] : 0);
      const g = (rt, h) => Math.pow(10, -3 * h / (rt * F));
      this.gB = [g(q.bass, hL), g(q.bass, hR)]; this.gM = [g(q.mid, hL), g(q.mid, hR)];
    }
    this.cx = 1 - Math.exp(-2 * Math.PI * q.xover / F);
    this.tb = q.treble >= F / 2 ? 1 : 1 - Math.exp(-2 * Math.PI * q.treble / F);
    this.depth = q.depth / 71;
    this.meT = q.me ? q.me * 32 : 0; // ticks between tap updates (estimate: 1.6–26 ms at 20 kHz)
    this.pitch = q.me ? q.pitch : 0;
    this.pairs = q.pairs;
    this.chDepth = q.depth / 71 * 0.008 * F;
    this.chT = Math.round((q.me || 8) * 0.04 * F);
  }

  _layout(fit = 1) {
    const P = LEX_PROGRAMS[this.prog], F = this.F, A = LEX_ALGS[P.alg];
    this.alg = P.alg;
    const maxPre = Math.ceil(P.pre[1] * F / 1000) + 2;
    let words = 0;
    const alloc = (len) => { const b = words; words += len + 2; return b; };
    this.preB = alloc(maxPre);
    if (P.alg === 'chorus') {
      this.chLen = Math.ceil(0.045 * F);
      this.chB = alloc(this.chLen);
      this.chAp = [0, 1, 2, 3].map(i => { const len = Math.round([0.0053, 0.0071, 0.0047, 0.0083][i] * F); return { b: alloc(len), len }; });
      this.chW = Array.from({ length: 8 }, () => ({ from: 0, to: 0, ctr: 1 + Math.floor(this._rand() * 2000) }));
      this.scale = 1;
    } else {
      const base = A.ind.reduce((a, b) => a + b, 0) + LEX_TANK.reduce((a, l, i) => a + l * A.mul[i], 0) + (A.ap3 ? A.ap3[0] + A.ap3[1] : 0);
      let sc = F / 29761 * P.size * fit;
      const room = this.memWords - maxPre - 3 * (A.ind.length + 12) - 80;
      if (base * sc > room) sc = Math.max(0.05, room / base);
      this.scale = sc / (F / 29761);
      const L = (n) => Math.max(2, Math.round(n * sc)) | 1;
      this.ind = A.ind.map(n => { const len = L(n); return { b: alloc(len), len }; });
      this.len = LEX_TANK.map((n, i) => L(n * A.mul[i]));
      this.tb_ = this.len.map((len, i) => alloc(len + ((i & 3) <= 1 ? 36 : 0)));
      this.ap3 = A.ap3 ? A.ap3.map(L) : null;
      this.ap3B = this.ap3 ? this.ap3.map(len => alloc(len)) : null;
      this.dd1 = A.dd1; this.er = A.er;
      const tap = (t, near) => { const [line, pos, sg] = t; return [line, Math.max(1, Math.round(pos / LEX_TANK[line] * this.len[line] * (near ? 0.3 : 1))), sg]; };
      this.tapsL = LEX_TAPS_L.map(t => tap(t)); this.tapsR = LEX_TAPS_R.map(t => tap(t));
      this.nearL = LEX_TAPS_L.map(t => tap(t, 1)); this.nearR = LEX_TAPS_R.map(t => tap(t, 1));
      this.walk = [0, 1, 2, 3].map(() => ({ from: 0, to: 0 })); this.meCtr = 1;
      this.lastL = 0; this.lastR = 0; this.xl = [0, 0]; this.tr = [0, 0];
    }
    this.words = words;
    if (words > this.memWords && fit > 0.5 && P.alg !== 'chorus') return this._layout(fit * (this.memWords - 8) / words);
    let size = 1024; while (size < words) size <<= 1;
    this.mem = new Float64Array(size); this.mask = size - 1; this.p = 0;
  }

  _w(a, v) { // write one fixed-point word, saturating. Magnitude truncation (estimate) lets tails die instead of limit-cycling.
    const q = this.wq; v = Math.trunc(v * q) / q;
    this.mem[a & this.mask] = v > 1 ? 1 : v < -1 ? -1 : v;
  }
  _r(a) { return this.mem[a & this.mask]; }
  _rf(a, d) { const i = Math.floor(d), f = d - i; return this._r(a + i) * (1 - f) + this._r(a + i + 1) * f; }
  _ap(b, len, g, x) { const p = this.p, vd = this._r(p + b + len), w = x + g * vd; this._w(p + b, w); return vd - g * w; }

  // gain-ranging 12-bit converter: the signal is boosted in 6 dB steps while it is small
  _conv(x, env) {
    let s = this.steps; while (s > 0 && env * (1 << s) >= 1) s--;
    const g = 1 << s; let y = x * g; y = y > 1 ? 1 : y < -1 ? -1 : y;
    return Math.round(y * this.adcQ) / this.adcQ / g;
  }

  _tick(x) {
    const p = this.p = (this.p - 1) & this.mask;
    this._w(p + this.preB, x);
    const u0 = this._r(p + this.preB + this.preD);
    if (this.alg === 'chorus') { this._chorus(u0); return; }
    // input diffusers
    let u = u0, uE = 0;
    for (let i = 0; i < this.ind.length; i++) { u = this._ap(this.ind[i].b, this.ind[i].len, this.gIn[i], u); if (i === this.ind.length - 2) uE = u; }
    // Mode Enhancement: each tap walks one sample at a time, at random, inside ±Pitch
    const W = this.walk;
    if (this.meT) {
      if (--this.meCtr <= 0) {
        this.meCtr = this.meT;
        for (const w of W) { w.from = w.to; let n = w.to + (this._rand() < 0.5 ? -1 : 1); if (Math.abs(n) > this.pitch) n = w.to - Math.sign(n - w.to); w.to = n; }
      }
    } else { this.meCtr = 1; for (const w of W) { w.from = w.to = 0; } }
    const ph = this.meT ? 1 - this.meCtr / this.meT : 0;
    const len = this.len, B = this.tb_;
    let out0 = 0, out1 = 0;
    for (let s = 0; s < 2; s++) {
      const o = s * 4;
      let a = u + (s ? this.lastL : this.lastR);
      // modulated allpass (decay diffusion 1)
      const vd = this._rf(p + B[o], len[o] + 16 + (W[s].from + (W[s].to - W[s].from) * ph)), g = -this.dd1, w = a + g * vd;
      this._w(p + B[o], w); a = vd - g * w;
      // delay 1, read through a wandering tap
      this._w(p + B[o + 1], a);
      let v = this._rf(p + B[o + 1], len[o + 1] + 16 + (W[2 + s].from + (W[2 + s].to - W[2 + s].from) * ph));
      // Bass / Mid decay split at Crossover, then Treble decay
      this.xl[s] += this.cx * (v - this.xl[s]);
      v = this.gB[s] * this.xl[s] + this.gM[s] * (v - this.xl[s]);
      this.tr[s] += this.tb * (v - this.tr[s]); v = this.tr[s];
      // allpass (decay diffusion 2), optional third allpass, delay 2
      v = this._ap(B[o + 2], len[o + 2], this.dd2, v);
      if (this.ap3) v = this._ap(this.ap3B[s], this.ap3[s], -this.dd2 * 0.8, v);
      this._w(p + B[o + 3], v);
      if (s) out1 = this._r(p + B[o + 3] + len[o + 3]); else out0 = this._r(p + B[o + 3] + len[o + 3]);
    }
    this.lastL = out0; this.lastR = out1;
    // output taps; Depth crossfades near taps (plus the diffused input) to the far ones
    const d = this.depth;
    this.yl = 0.6 * (d * this._sum(this.tapsL, p, B) + (1 - d) * (this._sum(this.nearL, p, B) + this.er * u));
    this.yr = 0.6 * (d * this._sum(this.tapsR, p, B) + (1 - d) * (this._sum(this.nearR, p, B) + this.er * uE));
  }
  _sum(T, p, B) { let y = 0; for (let i = 0; i < T.length; i++) { const t = T[i]; y += t[2] * this._r(p + B[t[0]] + t[1]); } return y;
  }

  // P9: eight voices, four per side, each delay wandering at random; diffusion on pairs 3 and 4
  _chorus(u) {
    const p = this.p; this._w(p + this.chB, u);
    const y = [0, 0];
    for (let v = 0; v < 8; v++) {
      const w = this.chW[v];
      if (--w.ctr <= 0) { w.from = w.to; w.to = this._rand() * 2 - 1; w.ctr = this.chT; }
      const t = 1 - w.ctr / this.chT, sm = w.from + (w.to - w.from) * (0.5 - 0.5 * Math.cos(Math.PI * t));
      const pair = v >> 1, side = v & 1;
      const dl = Math.min(this.chLen - 2, (LEX_CHORUS_MS[pair] + side * 1.3) * 0.001 * this.F + this.chDepth * sm);
      let s = this._rf(p + this.chB, Math.max(1, dl));
      if (pair >= 2) { const a = this.chAp[(pair - 2) * 2 + side]; s = this._ap(a.b, a.len, this.dd2 * 1.2, s); }
      y[side] += this.pairs[pair] * s;
    }
    this.yl = 0.5 * y[0]; this.yr = 0.5 * y[1];
  }

  // one host-rate sample in, sets this.L / this.R
  run(x) {
    // input transformer: low-end roll-off and soft saturation
    this.hx = this.hxA * (this.hx + x - this.hxIn); this.hxIn = x;
    let v = this.xsat * Math.tanh(this.hx / this.xsat);
    v = lexBiq(this.aa, v * 0.5); // anti-alias filter; converter full scale is about ±2 V
    this.ph += this.ratio;
    while (this.ph >= 1) {
      this.ph -= 1;
      const back = this.ph / this.ratio, xi = v - (v - this.prev) * Math.min(1, back);
      const a = Math.abs(xi); this.envIn = a > this.envIn ? a : this.envIn * this.envRel;
      this._tick(this._conv(xi, this.envIn)); const yl = this.yl, yr = this.yr;
      const al = Math.abs(yl), ar = Math.abs(yr);
      this.envL = al > this.envL ? al : this.envL * this.envRel; this.envR = ar > this.envR ? ar : this.envR * this.envRel;
      this.zl = this._conv(yl, this.envL); this.zr = this._conv(yr, this.envR); // DAC with zero-order hold
    }
    this.prev = v;
    this.L = lexBiq(this.rcL, this.zl) * 2; this.R = lexBiq(this.rcR, this.zr) * 2;
  }

  memoryUse() { return { words: this.words, of: this.memWords, scale: this.scale }; }
}

// zero subnormal-bound numbers in an object's own state (plain numbers, small arrays, nested state objects)
const FLUSH_SKIP = new Set(['c', 'knobs', 'fx', 'lex', 'mem', 'C']);
function flushTiny(o, depth) {
  if (Array.isArray(o) || ArrayBuffer.isView(o)) {
    if (o.length > 64) return;
    for (let i = 0; i < o.length; i++) { const x = o[i]; if (typeof x === 'number') { if (x !== 0 && x < 1e-150 && x > -1e-150) o[i] = 0; } else if (x && typeof x === 'object' && depth < 2) flushTiny(x, depth + 1); }
    return;
  }
  for (const k in o) {
    const x = o[k];
    if (typeof x === 'number') { if (x !== 0 && x < 1e-150 && x > -1e-150) o[k] = 0; }
    else if (x && typeof x === 'object' && depth < 2 && !FLUSH_SKIP.has(k)) flushTiny(x, depth + 1);
  }
}

const RAT_OS = 2; // the RAT runs at 2x the voice rate (4x the host rate)

// Simple digital delay (outside the circuit): tempo-synced time, feedback, and a low cut and high cut
// inside the feedback loop, so every repeat gets thinner and darker. Clean digital repeats, no saturation.
// Ping-pong (default): the synth goes into the left line only and each line feeds the other, so repeats
// alternate left, right, left. Mono: one line, the same repeats on both sides.
const DLY_DIVS = [['1/32', 1 / 8], ['1/16T', 1 / 6], ['1/16', 1 / 4], ['1/8T', 1 / 3], ['1/16D', 3 / 8], ['1/8', 1 / 2], ['1/4T', 2 / 3], ['1/8D', 3 / 4], ['1/4', 1], ['1/4D', 3 / 2], ['1/2', 2]];
const dlyDiv = (p) => DLY_DIVS[Math.min(DLY_DIVS.length - 1, Math.round(p * (DLY_DIVS.length - 1)))];
const dlyLo = (p) => 20 * Math.pow(50, p);        // 20 Hz .. 1 kHz
const dlyHi = (p) => 1000 * Math.pow(20, p);      // 1 kHz .. 20 kHz
class DigitalDelay {
  constructor(fs) {
    this.fs = fs; this.buf = new Float32Array(Math.ceil(fs * 2.6)); this.bufR = new Float32Array(this.buf.length); this.w = 0;
    this.d = fs * 0.25; this.dT = this.d; this.lp = 0; this.hp = 0; this.hpIn = 0; this.lpR = 0; this.hpR = 0; this.hpInR = 0;
    this.l = 0; this.r = 0; this.pp = true;
  }
  derive(k, bpm) {
    const fs = this.fs;
    this.dT = Math.min(this.buf.length - 4, dlyDiv(k.dlytime)[1] * 60 / bpm * fs);
    this.fb = 0.95 * k.dlyfb;
    this.lpA = Math.exp(-2 * Math.PI * Math.min(dlyHi(k.dlyhi), 0.45 * fs) / fs);
    this.hpA = Math.exp(-2 * Math.PI * dlyLo(k.dlylo) / fs);
    this.mix = k.dlymix * k.dlymix * 1.2; // return level, roughly even in loudness
    this.slewA = 1 - Math.exp(-1 / (0.06 * fs)); // time changes glide over ~60 ms instead of clicking
    const pp = k.dlypp >= 0.5; if (pp !== this.pp) { this.pp = pp; this.bufR.fill(0); }
  }
  run(x) {
    const b = this.buf, B = this.bufR, N = b.length;
    this.d += (this.dT - this.d) * this.slewA;
    let r = this.w - this.d; if (r < 0) r += N;
    const i = Math.floor(r), f = r - i, j = i + 1 === N ? 0 : i + 1;
    let y = b[i] + (b[j] - b[i]) * f;
    // tone: one-pole low cut and high cut on what comes back
    this.hp = this.hpA * (this.hp + y - this.hpIn); this.hpIn = y; y = this.hp;
    this.lp = y + this.lpA * (this.lp - y); y = this.lp;
    if (this.pp) {
      let z = B[i] + (B[j] - B[i]) * f;
      this.hpR = this.hpA * (this.hpR + z - this.hpInR); this.hpInR = z; z = this.hpR;
      this.lpR = z + this.lpA * (this.lpR - z); z = this.lpR;
      b[this.w] = x + this.fb * z; B[this.w] = y; // left takes the input and the right's repeats; right takes the left's
      this.l = y * this.mix; this.r = z * this.mix;
    } else {
      b[this.w] = x + this.fb * y;
      this.l = this.r = y * this.mix;
    }
    this.w = this.w + 1 === N ? 0 : this.w + 1;
    return this.l;
  }
}

// Sidechain on the effect returns, after the Kilohearts Compactor (a limiter/ducker that works at sample
// level). The key is the dry synth. Threshold is the key level that ducks the returns to silence;
// Range shapes how the key maps onto that (below 100% quieter key levels are ignored, above 100% they
// duck harder, like a soft clipper on the key). Attack is lookahead: the returns are delayed by it and the
// ducking ramps in ahead of the transient. Hold keeps full ducking after the key drops, Release brings it
// back. Mode picks the detector: per-sample Peak, inter-sample peaks (ISP), or RMS. Mix blends in the
// unducked returns. With Attack and Release at 0 the gain follows the key waveform: ring-mod sidechaining.
// Our reading of the published control descriptions; Kilohearts does not publish its gain law.
const CMP_MODES = ['Peak', 'ISP', 'RMS'];
const cmpThrDb = (p) => -40 + 40 * p;
const cmpAtkMs = (p) => 10 * p;
const cmpHoldMs = (p) => 500 * p * p;
const cmpRelMs = (p) => 1000 * p * p;
class Compactor {
  constructor(fs) {
    this.fs = fs; this.L = new Float32Array(Math.ceil(fs * 0.011)); this.R = new Float32Array(this.L.length); this.w = 0;
    this.env = 0; this.hold = 0; this.ms = 0; this.k1 = 0; this.k2 = 0; this.k3 = 0; this.l = 0; this.r = 0; this.g = 1;
  }
  derive(k) {
    const fs = this.fs;
    this.thr = Math.pow(10, cmpThrDb(k.cmpthr) / 20);
    this.rng = 2 * k.cmprng;
    this.D = Math.min(this.L.length - 1, Math.round(cmpAtkMs(k.cmpatk) * 1e-3 * fs));
    this.atkA = this.D > 0 ? 1 - Math.exp(-3 / this.D) : 1;
    this.holdN = Math.round(cmpHoldMs(k.cmphold) * 1e-3 * fs);
    const rel = cmpRelMs(k.cmprel) * 1e-3 * fs;
    this.relA = rel > 0.5 ? Math.exp(-1 / rel) : 0;
    this.mode = Math.round(k.cmpmode * 2);
    this.rmsA = Math.exp(-1 / (0.01 * fs));
    this.mix = k.cmpmix;
  }
  // key -> duck amount 0..1 (1 = silence at threshold)
  _amount(det) {
    const x = det / this.thr, R = this.rng;
    let a;
    if (R <= 1) a = R > 0 ? (x - (1 - R)) / R : 0;
    else { const s = R - 1; a = (1 - s) * x + s * Math.tanh(3 * x) / 0.99505; }
    return a <= 0 ? 0 : a >= 1 ? 1 : a;
  }
  // key y, returns wl/wr: stores the delayed returns in l/r and gives the gain for them
  run(y, wl, wr) {
    let det;
    if (this.mode === 2) { this.ms = y * y + this.rmsA * (this.ms - y * y); det = Math.sqrt(this.ms * 2); } // sine peak = rms * sqrt 2
    else {
      det = Math.abs(y);
      if (this.mode === 1) { // inter-sample peak: Catmull-Rom midpoint between the last two samples
        const m = (-this.k3 + 9 * this.k2 + 9 * this.k1 - y) / 16; // Catmull-Rom midpoint of the previous two samples
        if (Math.abs(m) > det) det = Math.abs(m);
      }
      this.k3 = this.k2; this.k2 = this.k1; this.k1 = y;
    }
    const a = this._amount(det);
    if (a >= this.env) { this.env += (a - this.env) * this.atkA; this.hold = this.holdN; }
    else if (this.hold > 0) this.hold--;
    else this.env = a + (this.env - a) * this.relA;
    // lookahead: the returns come out D samples late, so the ramp lands on time
    const N = this.L.length; let ri = this.w - this.D; if (ri < 0) ri += N;
    this.L[this.w] = wl; this.R[this.w] = wr; this.l = this.L[ri]; this.r = this.R[ri]; this.w = this.w + 1 === N ? 0 : this.w + 1;
    this.g = 1 - this.mix * this.env;
    return this.g;
  }
}

class TB303Voice {
  constructor(sampleRate, components) {
    this.fsOut = sampleRate;
    this.os = 2;
    this.fs = sampleRate * this.os;
    this.h = 1 / this.fs;
    this.c = {};
    for (const k in COMPONENTS) this.c[k] = (components && k in components) ? components[k] : COMPONENTS[k].v;

    // front panel (0..1)
    this.knobs = { tuning: 0.5, cutoff: 0.35, resonance: 0.7, envmod: 0.6, decay: 0.3, accent: 0.7, volume: 0.8, dist: 0.5, filter: 0.4, ratvol: 0.5,
      lexprog: 0.125, lexbass: 0.3, lexmid: 0.27, lexxover: 0.443, lextreble: 0.834, lexdepth: 0.5, lexdiff: 0.55, lexpre: 0.2, lexme: 0.5, lexpitch: 0.2, lexmix: 0.5,
      cmpthr: 0.5, cmprng: 0.5, cmpatk: 0.3, cmphold: 0, cmprel: 0.4, cmpmix: 0.7, cmpmode: 0,
      dlypp: 1, dlytime: 0.7, dlyfb: 0.35, dlylo: 0.3, dlyhi: 0.55, dlymix: 0.35 };
    this.fx = { rat: false, lex: false, dly: false };
    this.bpm = 128;
    this.lex = new Lexicon224(sampleRate);
    this.dly = new DigitalDelay(sampleRate);
    this.cmp = new Compactor(sampleRate);
    this.rt = { hin: 0, xin: 0, h1: 0, h2: 0, x1: 0, fb: 0, y: 0, hc: 0, xc: 0, v: 0, f: 0, xp: 0, dco: 0, dci: 0 };
    this.waveform = 'saw';
    // microtuning: an offset per note of the scale (C..B, every octave), in volts on the 1 V/oct CV, so 1 cent = 1/1200 V
    this.tuneCv = new Float64Array(12);

    // state
    this.cvTarget = dacVoltage(33, this.c.VREF, this.c.R_DAC, this.c.R2_DAC);
    this.cv = this.cvTarget;
    this.slide = false;
    this.gate = false;
    this.accentNote = false;
    this.vSaw = this.c.V_LO;
    this.sawDelay = this.vSaw; // one-sample delay for polyBLEP
    this.sqHp = 0; this.sqPrevIn = this.c.V_LO; this.sqLp = 0;
    this.v = new Float64Array(4); // ladder cap voltages
    this.fbLp = 0;
    this.envVcf = 0; this.envAttack = 0;
    this.envVca = 0; this.vcaAttack = 0;
    this.vAcc = 0; this.vWowC = 0; this.vWow = 0;
    this.rng = 0x9e3779b9 ^ ((Math.random() * 0x7fffffff) | 0); // per-voice xorshift32 for the noise sources
    this.hp14 = 0; this.hp14In = 0; this.hp38 = 0; this.hp38In = 0;
    this.hp49 = 0; this.hp49In = 0; this.hp76 = 0; this.hp76In = 0;
    this.dc = { z1: 0, z2: 0, z3: 0, z4: 0 };
    this.decim = this._makeDecimator();
    this._derive();
  }

  setComponent(name, value) { if (name in this.c) { this.c[name] = value; this._derive(); } }
  setFx(name, on) { if (name in this.fx) this.fx[name] = !!on; }
  setKnob(name, value) { if (name in this.knobs) { this.knobs[name] = value; this._derive(); } }
  // cents: 12 offsets, C first; the next note picks it up (a held note keeps its pitch)
  setTune(cents) { for (let i = 0; i < 12; i++) { const c = +(cents && cents[i]) || 0; this.tuneCv[i] = c / 1200; } }

  _derive() {
    const c = this.c, k = this.knobs;
    this.cmp.derive(k); this.dly.derive(k, this.bpm);
    // noise: density * sqrt(bandwidth) at the oversampled rate gives the rms per sample
    const nb = Math.sqrt(this.fs / 2) * c.NOISE;
    this.nVcf = c.EN_VCF * nb; this.nOta = c.EN_OTA * nb; this.nOut = c.EN_OUT * nb;
    this.noiseOn = this.nVcf + this.nOta + this.nOut > 0;
    // VCO: expo converter calibrated (TM4/TM5) to F_TUNE at CV_TUNE, 1 V/oct.
    // Ramp slope dV/dt = I/C33, frequency = I / (C33 * (V_HI - V_LO)).
    this.swing = c.V_HI - c.V_LO;
    this.iVcoRef = c.F_TUNE * c.C33 * this.swing; // collector current at CV_TUNE
    this.tuneOct = (k.tuning - 0.5) * 2; // VR2 50K(B) linear, ±1 oct
    // square shaper HPF (C10/R_SQB) and Q8 gain
    this.sqA = Math.exp(-1 / (c.C10 * c.R_SQB * this.fs));
    this.sqGain = c.R_SQC / c.R_SQE;
    this.sqLpA = Math.exp(-2 * Math.PI * 18000 / this.fs); // collector/Miller rounding
    // slide
    this.slideA = Math.exp(-1 / (c.R93 * c.C35 * this.fs));
    // VCF
    this.C = [c.C18, c.C19, c.C24, c.C26];
    this.inAtt = c.R98 / (c.R_IN + c.R98);
    this.kFb = c.A_Q21 * k.resonance; // VR4 linear (B) taper
    this.fbA = Math.exp(-1 / (c.C15 * c.R98 * this.fs)); // feedback HPF corner (~72 Hz)
    // Millman sum at Q10 base: G = 1/R
    // key track (KT > 0 closes the pot's switch): op-amp buffer drives KT * (CV - V_KT) into R_KT
    const g73 = 1 / c.R73, g72 = 1 / c.R72, g63 = 1 / c.R63, g71 = 1 / c.R71, gKt = c.KT > 0 ? 1 / c.R_KT : 0;
    const gs = g73 + g72 + g63 + g71 + gKt;
    this.wCut = g73 / gs; this.wEnv = g72 / gs; this.wAcc = g63 / gs; this.wKt = c.KT * gKt / gs;
    this.vCutPot = c.V_CUT * k.cutoff;
    this.envModFrac = audioTaper(k.envmod);
    // envelopes
    const rDecay = c.R138 + c.VR6 * audioTaper(k.decay);
    this.envDecA = Math.exp(-1 / (rDecay * c.C62 * this.fs));
    this.envDecAccA = Math.exp(-1 / (c.R138 * c.C62 * this.fs)); // IC12 shorts VR6 on accent
    this.envAtkA = Math.exp(-1 / (c.R152 * c.C62 * this.fs));
    this.vcaAtkA = Math.exp(-1 / (c.R49 * c.C67 * this.fs));                // accented: fast
    this.vcaSoftA = Math.exp(-1 / ((c.R49 + c.R_SOFT) * c.C67 * this.fs)); // normal: soft attack
    this.vcaDecA = Math.exp(-1 / (c.R52 * c.C67 * this.fs));
    this.vcaRelA = Math.exp(-1 / (c.R_VREL * c.C67 * this.fs));
    // accent sweep
    this.accFrac = k.accent; // VR7 linear
    this.accChgK = 1 / (c.R117 * c.C_ACC * this.fs);
    this.accDisA = Math.exp(-1 / (c.R_ACC * c.C_ACC * this.fs));
    // wow: VR7 -> D24 -> R_ACC -> VR4 gang 2 track -> C_ACC; the gang wiper drives the filter. Resonance picks the tap:
    // low takes the fast end (a sharp accent), high the cap end (a swell that builds over consecutive accents).
    {
      const ra = Math.max(c.VR4 * k.resonance, 1), rb = Math.max(c.VR4 * (1 - k.resonance), 1);
      const rs = c.R_ACC + c.VR7 * k.accent * (1 - k.accent);
      const gl = 1 / (c.R63 + 1 / (g73 + g72 + g71 + gKt)), gb = 1 / rb, dt = 1 / this.fs;
      const tap = (gsrc) => { const sum = gsrc + gb + gl; return { a: gsrc / sum, b: gb / sum, tgt: gsrc / (gsrc + gl), k: Math.exp(-dt * gb * (gsrc + gl) / sum / c.C_ACC) }; };
      this.wowOn = tap(1 / (rs + ra)); this.wowOff = tap(0);
    }
    // couplings
    this.hp14A = Math.exp(-1 / (c.C14 * c.R85 * this.fs));
    this.hp49A = Math.exp(-1 / (c.C49 * c.R88 * this.fs));
    this.hp76A = Math.exp(-1 / (c.C76 * c.R100 * this.fs));
    this.hp38A = Math.exp(-1 / (c.C38 * c.R_OUT * this.fs));
    this.vinDen = 1 / c.R88 + 1 / c.R100 + 1 / c.R101;
    this.vol = audioTaper(k.volume);
    this._deriveFx();
  }

  _deriveFx() {
    const c = this.c, k = this.knobs, fs = this.fs * RAT_OS, e = (tau) => Math.exp(-1 / (Math.max(tau, 1e-12) * fs));
    // RAT: LM308 non-inverting stage. It runs RAT_OS times faster than the voice, because the rail clip,
    // slew limit and diodes turn a loud resonant peak into harmonics far above 20 kHz that otherwise fold
    // back down as inharmonic whine.
    if (!this.ratLP) {
      const fc = 22000 / fs, w = 2 * Math.PI * fc, cs = Math.cos(w);
      this.ratLP = [0.5176, 0.7071, 1.9319].map(Q => { const al = Math.sin(w) / (2 * Q), a0 = 1 + al; return { b0: (1 - cs) / 2 / a0, b1: (1 - cs) / a0, b2: (1 - cs) / 2 / a0, a1: -2 * cs / a0, a2: (1 - al) / a0, x1: 0, x2: 0, y1: 0, y2: 0 }; });
    }
    this.ratDcA = Math.exp(-2 * Math.PI * 5 / this.fs); // output coupling: blocks the DC that asymmetric clipping leaves
    const rd = c.RAT_RD * audioTaper(k.dist) + 1;
    this.ratRd = rd;
    this.ratInA = e(c.RAT_CIN * 1e6);
    this.ratA1 = e(c.RAT_RG1 * c.RAT_CG1); this.ratA2 = e(c.RAT_RG2 * c.RAT_CG2);
    this.ratFA = e(rd * c.RAT_CF);
    const comp = 30e-12 / c.RAT_CC;
    const gHf = 1 + rd * (1 / c.RAT_RG1 + 1 / c.RAT_RG2);
    this.ratGain = gHf;
    this.ratK = Math.min(1, 2 * Math.PI * c.RAT_GBW * comp / gHf / fs);
    this.ratSlew = c.RAT_SR * 1e6 * comp / fs;
    this.ratRail = Math.max(c.RAT_VS / 2 - 1, 0.5);
    this.ratCA = e(4.7e-6 * (c.RAT_RCL + 1e3));
    this.ratN = 1.8 * VT;
    this.ratIs = 1e-3 / Math.exp(c.RAT_VF / this.ratN);
    this.ratFltA = e((c.RAT_RF + 100e3 * k.filter) * c.RAT_CFLT);
    this.ratLevel = audioTaper(k.ratvol) * 6;
    this.lex.derive(c, k);
  }

  _rat(x) {
    const s = this.rt, x0 = s.xp; s.xp = x;
    let y = 0;
    for (let i = 1; i <= RAT_OS; i++) {
      let z = this._ratCore(x0 + (x - x0) * (i / RAT_OS));
      for (const q of this.ratLP) { const o = q.b0 * z + q.b1 * q.x1 + q.b2 * q.x2 - q.a1 * q.y1 - q.a2 * q.y2; q.x2 = q.x1; q.x1 = z; q.y2 = q.y1; q.y1 = o; z = o; }
      y = z;
    }
    s.dco = this.ratDcA * (s.dco + y - s.dci); s.dci = y;
    return s.dco;
  }

  _ratCore(x) {
    const s = this.rt, c = this.c;
    s.hin = this.ratInA * (s.hin + x - s.xin); s.xin = x;
    const u = s.hin;
    // current through the two RC gain legs (inverting input follows u)
    s.h1 = this.ratA1 * (s.h1 + u - s.x1); s.h2 = this.ratA2 * (s.h2 + u - s.x1); s.x1 = u;
    const iv = (s.h1 / c.RAT_RG1 + s.h2 / c.RAT_RG2) * this.ratRd;
    s.fb = iv + this.ratFA * (s.fb - iv);                        // pot || 100p
    const target = u + s.fb;
    let dy = (target - s.y) * this.ratK;                         // gain-bandwidth limit
    if (dy > this.ratSlew) dy = this.ratSlew; else if (dy < -this.ratSlew) dy = -this.ratSlew; // slew limit
    s.y = Math.max(-this.ratRail, Math.min(this.ratRail, s.y + dy));
    s.hc = this.ratCA * (s.hc + s.y - s.xc); s.xc = s.y;         // 4.7µ output coupling
    // diode pair to ground through RAT_RCL: (w - v)/R = 2 Is sinh(v / nVt)
    const w = s.hc, R = c.RAT_RCL, n = this.ratN, Is = this.ratIs;
    // Safeguarded Newton: the root lies between 0 and w, so keep it bracketed and bisect whenever a Newton
    // step would leave the bracket. Plain Newton from the last sample could not follow a fast swing into
    // the diodes' exponential region in 4 steps and left v volts off, which was heard as spitting and fizz.
    let lo = w < 0 ? w : 0, hi = w < 0 ? 0 : w;
    let v = Math.min(hi, Math.max(lo, s.v));
    for (let i = 0; i < 40; i++) {
      const t = Math.min(Math.max(v / n, -80), 80), e = Math.exp(t), ei = 1 / e;
      const f = (w - v) / R - Is * (e - ei), df = -1 / R - Is * (e + ei) / n;
      if (f > 0) lo = v; else hi = v;
      let nv = v - f / df;
      if (!(nv > lo && nv < hi)) nv = 0.5 * (lo + hi);
      const dv = nv - v; v = nv;
      if (dv < 1e-7 && dv > -1e-7) break;
    }
    s.v = v;
    s.f = v + this.ratFltA * (s.f - v);                          // FILTER: (1.5k + pot) with 3.3n
    return s.f * this.ratLevel;
  }

  // note: 0..63 DAC code (33 = A, 110 Hz)
  noteOn(code, accent, slide) {
    code = Math.max(0, Math.min(63, code | 0));
    this.cvTarget = dacVoltage(code, this.c.VREF, this.c.R_DAC, this.c.R2_DAC) + this.tuneCv[code % 12];
    const legato = this.gate && slide;
    this.slide = !!slide;
    if (!legato) {
      if (!this.slide) this.cv = this.cvTarget;
      this.accentNote = !!accent;
      this.envAttack = 1; // trigger VCF env (gate edge into Q38)
      this.vcaAttack = 1;
    }
    this.gate = true;
  }
  setSlide(on) { this.slide = !!on; }
  noteOff() { this.gate = false; }

  _makeDecimator() {
    // 2 cascaded biquads (4th-order Butterworth) at 0.42 * fsOut, running at fs
    const mk = (Q) => {
      const w = 2 * Math.PI * 0.42 * this.fsOut / this.fs;
      const al = Math.sin(w) / (2 * Q), cs = Math.cos(w), a0 = 1 + al;
      return { b0: (1 - cs) / 2 / a0, b1: (1 - cs) / a0, b2: (1 - cs) / 2 / a0, a1: -2 * cs / a0, a2: (1 - al) / a0, x1: 0, x2: 0, y1: 0, y2: 0 };
    };
    return [mk(0.5412), mk(1.3066)];
  }

  _vcfStep(u, Iladder) {
    // ladder states v[0..3]; branch b0 = input pair diff voltage x,
    // b1..b3 = differences between stages, b4 = top stage to fixed bases.
    // Same maths as before, written out without per-sample array allocation (it was most of the voice's CPU).
    const v = this.v, C = this.C, h2 = this.h / 2, twoVt = 2 * VT, Ih = Iladder / 2, G0 = Iladder / (4 * VT);
    const k = this.kFb;
    const uEff = u + k * this.fbLp;           // x = u - k*(v4 - LP(v4)) = uEff - k*v4
    const v0 = v[0], v1 = v[1], v2 = v[2], v3 = v[3];
    const b0 = uEff - k * v3, b1 = v0 - v1, b2 = v1 - v2, b3 = v2 - v3, b4 = v3;
    const T0 = Ih * Math.tanh(b0 / twoVt), T1 = Ih * Math.tanh(b1 / twoVt), T2 = Ih * Math.tanh(b2 / twoVt), T3 = Ih * Math.tanh(b3 / twoVt), T4 = Ih * Math.tanh(b4 / twoVt);
    const G0_ = Math.abs(b0) > 1e-9 ? T0 / b0 : G0, G1 = Math.abs(b1) > 1e-9 ? T1 / b1 : G0, G2 = Math.abs(b2) > 1e-9 ? T2 / b2 : G0,
      G3 = Math.abs(b3) > 1e-9 ? T3 / b3 : G0, G4 = Math.abs(b4) > 1e-9 ? T4 / b4 : G0;
    const C0 = C[0], C1 = C[1], C2 = C[2], C3 = C[3];
    // A = I - h/2 M (M linearised with feedback folded in), r = v + h/2 (f + B uEff)
    let a00 = 1 + h2 * G1 / C0, a01 = -h2 * G1 / C0, a02 = 0, a03 = h2 * G0_ * k / C0;
    let a10 = -h2 * G1 / C1, a11 = 1 + h2 * (G1 + G2) / C1, a12 = -h2 * G2 / C1, a13 = 0;
    let a20 = 0, a21 = -h2 * G2 / C2, a22 = 1 + h2 * (G2 + G3) / C2, a23 = -h2 * G3 / C2;
    let a30 = 0, a31 = 0, a32 = -h2 * G3 / C3, a33 = 1 + h2 * (G3 + G4) / C3;
    let r0 = v0 + h2 * (T0 - T1) / C0 + h2 * (G0_ / C0) * uEff, r1 = v1 + h2 * (T1 - T2) / C1, r2 = v2 + h2 * (T2 - T3) / C2, r3 = v3 + h2 * (T3 - T4) / C3;
    // Gaussian elimination (well-conditioned, diagonally dominant); zero entries skipped as before
    let m = a10 / a00;
    if (m !== 0) { a11 -= m * a01; a12 -= m * a02; a13 -= m * a03; r1 -= m * r0; }
    m = a21 / a11;
    if (m !== 0) { a22 -= m * a12; a23 -= m * a13; r2 -= m * r1; }
    m = a32 / a22;
    if (m !== 0) { a33 -= m * a23; r3 -= m * r2; }
    const n3 = r3 / a33;
    const n2 = (r2 - a23 * n3) / a22;
    const n1 = (r1 - a12 * n2 - a13 * n3) / a11;
    const n0 = (r0 - a01 * n1 - a02 * n2 - a03 * n3) / a00;
    v[0] = n0; v[1] = n1; v[2] = n2; v[3] = n3;
    // feedback-path HPF (C15) tracked as LP of v4
    this.fbLp = n3 + this.fbA * (this.fbLp - n3);
    return n3;
  }

  // one sample at the oversampled rate
  _tick() {
    const c = this.c;
    // --- slide / CV
    if (this.slide) this.cv = this.cvTarget + this.slideA * (this.cv - this.cvTarget);
    else this.cv = this.cvTarget;

    // --- VCO: I = Iref * 2^(CV - CV_TUNE + tune)
    const I = this.iVcoRef * Math.pow(2, this.cv - c.CV_TUNE + this.tuneOct);
    const dv = I / c.C33 * this.h;
    this.vSaw += dv;
    // polyBLEP around the reset: output is delayed one sample so the sample
    // before the step can be corrected too.
    let corrNow = 0;
    if (this.vSaw >= c.V_HI) {
      // Q27/Q28 discharge: fast reset, placed with sub-sample accuracy
      this.vSaw -= this.swing;
      const ph = Math.min(Math.max((this.vSaw - c.V_LO) / dv, 0), 1); // samples since crossing
      this.sawDelay -= 0.5 * this.swing * ph * ph;
      corrNow = 0.5 * this.swing * (1 - ph) * (1 - ph);
    }
    const saw0 = this.sawDelay;
    this.sawDelay = this.vSaw + corrNow;
    const saw = saw0;

    let wave;
    if (this.waveform === 'square') {
      // C10/R_SQB high-pass into Q8 base, PNP switch with emitter degeneration
      this.sqHp = this.sqA * (this.sqHp + saw - this.sqPrevIn);
      this.sqPrevIn = saw;
      const sw = c.SQ_HI - c.SQ_LO;
      const lin = 0.5 - this.sqHp * this.sqGain / sw; // base falls -> Q8 conducts -> output high
      const sat = lin <= 0 ? 0 : lin >= 1 ? 1 : lin * lin * (3 - 2 * lin); // soft saturation
      const out = c.SQ_LO + sw * sat;
      this.sqLp = out + this.sqLpA * (this.sqLp - out);
      wave = this.sqLp - (c.SQ_LO + c.SQ_HI) / 2;
    } else {
      this.sqPrevIn = saw;
      wave = saw - (c.V_LO + c.V_HI) / 2;
    }

    // --- envelopes
    // VCF env: gate edge charges C62 through R152, then decays via R138+VR6
    if (this.envAttack > 0) {
      this.envVcf = c.V_ENV + this.envAtkA * (this.envVcf - c.V_ENV);
      if (this.envVcf > 0.99 * c.V_ENV) this.envAttack = 0;
    } else {
      this.envVcf *= this.accentNote ? this.envDecAccA : this.envDecA;
    }
    // VCA env: C67 charges via R49 while gated, decays via R52, released on gate off
    if (this.gate) {
      if (this.vcaAttack > 0) {
        this.envVca = 1 + (this.accentNote ? this.vcaAtkA : this.vcaSoftA) * (this.envVca - 1);
        if (this.envVca > 0.99) this.vcaAttack = 0;
      } else this.envVca *= this.vcaDecA;
    } else this.envVca *= this.vcaRelA;
    // accent sweep: VCF env * VR7 through D27/R117 into C_ACC, bled by R_ACC
    const vAccIn = this.accentNote ? this.envVcf * this.accFrac : 0;
    const drive = vAccIn - c.V_D - this.vAcc;
    if (drive > 0) this.vAcc += drive * this.accChgK;
    this.vAcc *= this.accDisA;
    if (c.WOW > 0 || this.vWowC > 1e-6) {
      const vs = vAccIn - c.V_D, t = vs > this.wowOff.b * this.vWowC ? this.wowOn : this.wowOff;
      this.vWow = t.a * vs + t.b * this.vWowC;
      const tgt = t === this.wowOn ? t.tgt * vs : 0;
      this.vWowC = tgt + t.k * (this.vWowC - tgt);
    }
    const vAccF = c.WOW > 0 ? this.vAcc + c.WOW * (this.vWow - this.vAcc) : this.vAcc;

    // --- VCF control: Millman sum at Q10 base, expo current
    const vEnvMod = this.accentNote ? 0 : this.envVcf * this.envModFrac;
    let vb = this.wCut * this.vCutPot + this.wEnv * vEnvMod + this.wAcc * vAccF;
    if (this.wKt > 0) vb += this.wKt * (this.cv - c.V_KT);
    const Il = Math.min(c.I_REF * Math.exp(vb / VT), 2e-3);

    // approx. unit gaussian from four uniforms (xorshift32), one per source
    let nV = 0, nO = 0, nX = 0;
    if (this.noiseOn) {
      const g = () => { let a = 0; for (let i = 0; i < 4; i++) { let x = this.rng; x ^= x << 13; x ^= x >>> 17; x ^= x << 5; this.rng = x; a += (x >>> 0) / 4294967296; } return (a - 2) * 1.7320508; };
      nV = g() * this.nVcf; nO = g() * this.nOta; nX = g() * this.nOut;
    }
    const y = this._vcfStep(wave * this.inAtt + nV, Il);

    // --- output amp -> C14 coupling -> OTA VCA
    const ya = y * c.A_OUT; // Q21 diff stage -> Q19 output buffer
    this.hp14 = this.hp14A * (this.hp14 + ya - this.hp14In); this.hp14In = ya;
    const vo = this.hp14;
    // into the OTA: filter via C49/R88 plus resonance compensation (VR4 wiper) via C76/R100
    this.hp49 = this.hp49A * (this.hp49 + vo - this.hp49In); this.hp49In = vo;
    const vr = vo * this.knobs.resonance;
    this.hp76 = this.hp76A * (this.hp76 + vr - this.hp76In); this.hp76In = vr;
    const vin = (this.hp49 / c.R88 + this.hp76 / c.R100) / this.vinDen + nO;
    const Iabc = Math.max(this.envVca * c.V_VCA - 0.6, 0) / c.R105 + Math.max(this.vAcc - 0.6, 0) / (c.R25 + c.R26);
    const iout = Iabc * Math.tanh(vin / (2 * VT));
    const vout = iout * c.R_OTA;
    this.hp38 = this.hp38A * (this.hp38 + vout - this.hp38In); this.hp38In = vout;
    let o = this.hp38 * this.vol * 4.0 + nX * 4.0;
    if (this.fx.rat) o = this._rat(o);
    return 0.95 * Math.tanh(o / 0.95); // output stage headroom
  }

  // the effects after the dry signal y: the 224 and the delay are parallel returns, both ducked by the
  // Compactor keyed from y, summed with y, then the output soft clip
  _fxOut(y, out, outR, n) {
    const lex = this.fx.lex ? this.lex : null, dly = this.fx.dly ? this.dly : null;
    let wl = 0, wr = 0;
    if (lex) { lex.run(y); wl += lex.wet * lex.L; wr += lex.wet * lex.R; }
    if (dly) { dly.run(y); wl += dly.l; wr += dly.r; }
    const g = this.cmp.run(y, wl, wr);
    out[n] = 0.98 * Math.tanh((y + this.cmp.l * g) / 0.98);
    if (outR) outR[n] = 0.98 * Math.tanh((y + this.cmp.r * g) / 0.98);
  }

  _decimate(x) {
    for (const s of this.decim) {
      const y = s.b0 * x + s.b1 * s.x1 + s.b2 * s.x2 - s.a1 * s.y1 - s.a2 * s.y2;
      s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y; x = y;
    }
    return x;
  }

  // Decaying filter and envelope states eventually reach subnormal floats, which run many times
  // slower on most CPUs (CPU climbed 3x over 15 s of silence). Snap anything that small to zero once
  // per block. 1e-150 is far below any audible level and far below every component value.
  _flush() { flushTiny(this, 0); if (this.lex) flushTiny(this.lex, 0); flushTiny(this.dly, 0); flushTiny(this.cmp, 0); }

  // out is mono, or the left channel when outR is given
  process(out, start = 0, end = out.length, outR) {
    this._flush();
    const fx = this.fx.lex || this.fx.dly;
    for (let n = start; n < end; n++) {
      let y = 0;
      for (let o = 0; o < this.os; o++) y = this._decimate(this._tick());
      if (fx) this._fxOut(y, out, outR, n);
      else { out[n] = y; if (outR) outR[n] = y; }
    }
  }

  // diagnostics
  vcfResonantHz(vb) { // linear-theory peak for given base voltage
    const I = this.c.I_REF * Math.exp(vb / VT);
    return 1.3838 * (I / (4 * VT)) / (2 * Math.PI * this.c.C19);
  }
}

// Knob and part changes glide to their new value (one-pole, ~15 ms) instead of jumping, so turning a
// control, MIDI CC steps or a preset load do not zipper the sound. This sits outside the circuit:
// it only decides what value the voice sees each 32-sample tick. Discrete controls switch at once.
class ParamSlew {
  constructor(voice, fs, tau = 0.015) { this.v = voice; this.fs = fs; this.tau = tau; this.t = new Map(); }
  static instant(kind, name) { return name === 'lexprog' || name.startsWith('LEX_'); }
  set(kind, name, value) {
    const v = this.v;
    if (ParamSlew.instant(kind, name) || !isFinite(value)) { this.t.delete(kind + name); kind === 'k' ? v.setKnob(name, value) : v.setComponent(name, value); return; }
    const cur = kind === 'k' ? v.knobs[name] : v.c[name];
    if (cur === undefined) return;
    this.t.set(kind + name, { kind, name, value, log: kind === 'c' && value > 0 && cur > 0 });
  }
  get active() { return this.t.size > 0; }
  tick(n) {
    if (!this.t.size) return;
    const v = this.v, a = 1 - Math.exp(-n / (this.tau * this.fs));
    for (const [key, e] of this.t) {
      const obj = e.kind === 'k' ? v.knobs : v.c, cur = obj[e.name];
      let nx = e.log ? Math.exp(Math.log(cur) + (Math.log(e.value) - Math.log(cur)) * a) : cur + (e.value - cur) * a;
      if (Math.abs(nx - e.value) <= 1e-4 * Math.max(1e-9, Math.abs(e.value)) || (!e.log && Math.abs(nx - e.value) < 1e-5)) { nx = e.value; this.t.delete(key); }
      obj[e.name] = nx;
    }
    v._derive();
  }
}

// De-click: an output stage outside the circuit. The real 303 clicks where the VCA opens in about
// 0.1 ms (accented notes skip the soft attack) and where a retrigger lands on a ringing tail. The
// sequencer knows exactly when each retrigger happens, so the output is delayed by a few ms and the
// level eases to zero and back over `fadeMs` around each retrigger. Between retriggers the signal
// passes through untouched. Slides (no retrigger) are left alone. Note-offs are optional: the VCA
// already releases over about 5 ms there, so they are skipped unless `offs` is set.
class DeClick {
  constructor(voice, fs, { on = true, fadeMs = 1.5, lookMs = 3, offs = false } = {}) {
    this.v = voice; this.fs = fs; this.D = Math.max(1, Math.round(lookMs * 1e-3 * fs));
    this.bufL = new Float32Array(this.D); this.w = 0;
    this.t = 0; this.ev = []; this.offs = offs; this.on = on; this.setFade(fadeMs);
    const on0 = voice.noteOn.bind(voice), off0 = voice.noteOff.bind(voice);
    voice.noteOn = (code, accent, slide) => { if (!(voice.gate && slide)) this.mark(); on0(code, accent, slide); };
    voice.noteOff = () => { if (voice.gate && this.offs) this.mark(); off0(); };
  }
  setFade(ms) { this.F = Math.max(1, Math.min(this.D, Math.round(ms * 1e-3 * this.fs))); }
  setOn(on) { if (on === this.on) return; this.on = on; this.bufL.fill(0); this.ev.length = 0; }
  mark() { if (this.on) this.ev.push(this.t); }
  process(out, start = 0, end = out.length, outR) {
    const v = this.v;
    if (!this.on) { v.process(out, start, end, outR); this.t += end - start; return; }
    // run the voice dry and do the returns here, after the fade, so a retrigger never dips the reverb or delay tails
    const lex = v.fx.lex, dly = v.fx.dly, fx = lex || dly;
    v.fx.lex = v.fx.dly = false; v.process(out, start, end); v.fx.lex = lex; v.fx.dly = dly;
    const D = this.D, F = this.F, L = this.bufL, ev = this.ev;
    for (let n = start; n < end; n++, this.t++) {
      let y = L[this.w]; L[this.w] = out[n]; this.w = this.w + 1 === D ? 0 : this.w + 1;
      // yl/yr belong to time t - D; ease the level around any retrigger within F of it
      const tt = this.t - D;
      while (ev.length && ev[0] <= tt - F) ev.shift();
      if (ev.length) {
        let g = 1;
        for (let i = 0; i < ev.length; i++) {
          const d = Math.abs(tt - ev[i]); if (d < F) { const s = 0.5 - 0.5 * Math.cos(Math.PI * d / F); if (s < g) g = s; }
        }
        y *= g;
      }
      if (fx) v._fxOut(y, out, outR, n);
      else { out[n] = y; if (outR) outR[n] = y; }
    }
  }
}

// The player around the voice: parameter slew, de-click, and the step sequencer that walks a pattern.
// The AudioWorklet and the main-thread fallback both run this, so they sound the same.
// Messages: knob, component, wave, tune, fx, declick, pattern, bpm, groove (shuffle, gate length), play.
class TB303Engine {
  constructor(fs, onStep = () => {}) {
    this.fs = fs; this.onStep = onStep;
    this.v = new TB303Voice(fs); this.slew = new ParamSlew(this.v, fs); this.dc = new DeClick(this.v, fs);
    this.playing = false; this.bpm = 128; this.pattern = []; this.shuffle = 0; this.gate = 0.5;
    this.step = -1; this.ctr = 0; this.stepLen = 1; this.gateOffAt = -1;
  }
  message(m) {
    const v = this.v;
    if (m.type === 'knob') this.slew.set('k', m.name, m.value);
    else if (m.type === 'component') this.slew.set('c', m.name, m.value);
    else if (m.type === 'wave') v.waveform = m.value;
    else if (m.type === 'tune') v.setTune(m.value);
    else if (m.type === 'fx') v.setFx(m.name, m.value);
    else if (m.type === 'declick') { this.dc.setOn(!!m.on); this.dc.setFade(m.ms); }
    else if (m.type === 'pattern') this.pattern = m.value;
    else if (m.type === 'bpm') { this.bpm = m.value; v.bpm = m.value; v.dly.derive(v.knobs, m.value); }
    else if (m.type === 'groove') { this.shuffle = m.shuffle; this.gate = m.gate; }
    else if (m.type === 'play') { this.playing = m.value; this.step = -1; this.ctr = 0; if (!m.value) v.noteOff(); }
  }
  // a 16th note; shuffle lengthens the even steps and shortens the odd ones. Returns its length in samples.
  _startStep() {
    const P = this.pattern, n = P.length; if (!n) return 0;
    this.step = (this.step + 1) % n;
    const s = P[this.step], prev = P[(this.step - 1 + n) % n];
    const len = Math.round(this.fs * 60 / this.bpm / 4 * (1 + (this.step & 1 ? -this.shuffle : this.shuffle)));
    if (s.gate) {
      this.v.noteOn(s.code, s.accent, prev.gate && prev.slide);
      this.gateOffAt = s.slide ? -1 : Math.max(1, Math.round(len * this.gate));
    } else { this.v.noteOff(); this.gateOffAt = -1; }
    this.onStep(this.step);
    return len;
  }
  // fills out[s..e) (and outR, if given), split into runs at step starts, gate-offs and every 32 samples while slewing
  render(out, outR, s = 0, e = out.length) {
    let i = s;
    while (i < e) {
      if (this.playing && this.ctr <= 0) { this.stepLen = this._startStep() || 1; this.ctr = this.stepLen; }
      let run = e - i;
      if (this.slew.active) { run = Math.min(run, 32); this.slew.tick(run); }
      if (this.playing) {
        run = Math.min(run, this.ctr);
        const elapsed = this.stepLen - this.ctr;
        if (this.gateOffAt > elapsed) run = Math.min(run, this.gateOffAt - elapsed);
        else if (this.gateOffAt === elapsed) { this.v.noteOff(); this.gateOffAt = -1; }
      }
      this.dc.process(out, i, i + run, outR);
      i += run; if (this.playing) this.ctr -= run;
    }
  }
}

if (typeof module !== 'undefined' && module.exports) module.exports = { DigitalDelay, Compactor, DLY_DIVS, CMP_MODES, dlyDiv, dlyLo, dlyHi, cmpThrDb, cmpAtkMs, cmpHoldMs, cmpRelMs, TB303Voice, ParamSlew, DeClick, TB303Engine, COMPONENTS, dacVoltage, VT, Lexicon224, LEX_PROGRAMS, lexParams };
