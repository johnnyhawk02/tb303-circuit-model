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
  I_REF:  { v: 3.1e-6, unit: 'A', block: 'VCF',   source: 'estimate',  note: 'TM3 FREQ trim: ladder current at 0 V' },

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

  // --- VCA (IC15 BA662A OTA)
  C67:    { v: 1e-6,    unit: 'F', block: 'VCA',   source: 'td3',       note: 'VCA env cap (TD-3 C67 1/35)', mod: 'VCA envelope cap; scales VCA decay and attack together.' },
  R49:    { v: 100,     unit: 'Ω', block: 'VCA',   source: 'td3',       note: 'VCA env attack (TD-3 R49)', mod: 'VCA attack. Related to the Devil Fish Soft Attack control, which slows the attack of non-accented notes.' },
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

class TB303Voice {
  constructor(sampleRate, components) {
    this.fsOut = sampleRate;
    this.os = 2;
    this.fs = sampleRate * this.os;
    this.h = 1 / this.fs;
    this.c = {};
    for (const k in COMPONENTS) this.c[k] = (components && k in components) ? components[k] : COMPONENTS[k].v;

    // front panel (0..1)
    this.knobs = { tuning: 0.5, cutoff: 0.35, resonance: 0.7, envmod: 0.6, decay: 0.3, accent: 0.7, volume: 0.8 };
    this.waveform = 'saw';

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
    this.envVca = 0;
    this.vAcc = 0;
    this.hp14 = 0; this.hp14In = 0; this.hp38 = 0; this.hp38In = 0;
    this.hp49 = 0; this.hp49In = 0; this.hp76 = 0; this.hp76In = 0;
    this.dc = { z1: 0, z2: 0, z3: 0, z4: 0 };
    this.decim = this._makeDecimator();
    this._derive();
  }

  setComponent(name, value) { if (name in this.c) { this.c[name] = value; this._derive(); } }
  setKnob(name, value) { if (name in this.knobs) { this.knobs[name] = value; this._derive(); } }

  _derive() {
    const c = this.c, k = this.knobs;
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
    const g73 = 1 / c.R73, g72 = 1 / c.R72, g63 = 1 / c.R63, g71 = 1 / c.R71;
    const gs = g73 + g72 + g63 + g71;
    this.wCut = g73 / gs; this.wEnv = g72 / gs; this.wAcc = g63 / gs;
    this.vCutPot = c.V_CUT * k.cutoff;
    this.envModFrac = audioTaper(k.envmod);
    // envelopes
    const rDecay = c.R138 + c.VR6 * audioTaper(k.decay);
    this.envDecA = Math.exp(-1 / (rDecay * c.C62 * this.fs));
    this.envDecAccA = Math.exp(-1 / (c.R138 * c.C62 * this.fs)); // IC12 shorts VR6 on accent
    this.envAtkA = Math.exp(-1 / (c.R152 * c.C62 * this.fs));
    this.vcaAtkA = Math.exp(-1 / (c.R49 * c.C67 * this.fs));
    this.vcaDecA = Math.exp(-1 / (c.R52 * c.C67 * this.fs));
    this.vcaRelA = Math.exp(-1 / (c.R_VREL * c.C67 * this.fs));
    // accent sweep
    this.accFrac = k.accent; // VR7 linear
    this.accChgK = 1 / (c.R117 * c.C_ACC * this.fs);
    this.accDisA = Math.exp(-1 / (c.R_ACC * c.C_ACC * this.fs));
    // couplings
    this.hp14A = Math.exp(-1 / (c.C14 * c.R85 * this.fs));
    this.hp49A = Math.exp(-1 / (c.C49 * c.R88 * this.fs));
    this.hp76A = Math.exp(-1 / (c.C76 * c.R100 * this.fs));
    this.hp38A = Math.exp(-1 / (c.C38 * c.R_OUT * this.fs));
    this.vinDen = 1 / c.R88 + 1 / c.R100 + 1 / c.R101;
    this.vol = audioTaper(k.volume);
  }

  // note: 0..63 DAC code (33 = A, 110 Hz)
  noteOn(code, accent, slide) {
    code = Math.max(0, Math.min(63, code | 0));
    this.cvTarget = dacVoltage(code, this.c.VREF, this.c.R_DAC, this.c.R2_DAC);
    const legato = this.gate && slide;
    this.slide = !!slide;
    if (!legato) {
      if (!this.slide) this.cv = this.cvTarget;
      this.accentNote = !!accent;
      this.envAttack = 1; // trigger VCF env (gate edge into Q38)
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
    const v = this.v, C = this.C, h2 = this.h / 2, twoVt = 2 * VT, Ih = Iladder / 2;
    const k = this.kFb;
    const uEff = u + k * this.fbLp;           // x = u - k*(v4 - LP(v4)) = uEff - k*v4
    const x = uEff - k * v[3];
    const b = [x, v[0] - v[1], v[1] - v[2], v[2] - v[3], v[3]];
    const G = new Array(5), T = new Array(5);
    for (let i = 0; i < 5; i++) {
      T[i] = Ih * Math.tanh(b[i] / twoVt);
      G[i] = Math.abs(b[i]) > 1e-9 ? T[i] / b[i] : Iladder / (4 * VT);
    }
    const f = [(T[0] - T[1]) / C[0], (T[1] - T[2]) / C[1], (T[2] - T[3]) / C[2], (T[3] - T[4]) / C[3]];
    // linearised M (with feedback folded in), B for uEff
    const M = [
      [-G[1] / C[0], G[1] / C[0], 0, -G[0] * k / C[0]],
      [G[1] / C[1], -(G[1] + G[2]) / C[1], G[2] / C[1], 0],
      [0, G[2] / C[2], -(G[2] + G[3]) / C[2], G[3] / C[2]],
      [0, 0, G[3] / C[3], -(G[3] + G[4]) / C[3]],
    ];
    const B0 = G[0] / C[0];
    // (I - h/2 M) vn = v + h/2 (f + B uEff)
    const A = [], r = [];
    for (let i = 0; i < 4; i++) {
      A.push([0, 0, 0, 0]);
      for (let j = 0; j < 4; j++) A[i][j] = (i === j ? 1 : 0) - h2 * M[i][j];
      r.push(v[i] + h2 * f[i] + (i === 0 ? h2 * B0 * uEff : 0));
    }
    // Gaussian elimination (well-conditioned, diagonally dominant)
    for (let i = 0; i < 4; i++) {
      for (let rr = i + 1; rr < 4; rr++) {
        const m = A[rr][i] / A[i][i];
        if (m === 0) continue;
        for (let j = i; j < 4; j++) A[rr][j] -= m * A[i][j];
        r[rr] -= m * r[i];
      }
    }
    for (let i = 3; i >= 0; i--) {
      let s = r[i];
      for (let j = i + 1; j < 4; j++) s -= A[i][j] * v[j];
      v[i] = s / A[i][i];
    }
    // feedback-path HPF (C15) tracked as LP of v4
    this.fbLp = v[3] + this.fbA * (this.fbLp - v[3]);
    return v[3];
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
    const dt = dv / this.swing; // phase increment
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
      if (this.envAttack > 0) this.envVca = 1 + this.vcaAtkA * (this.envVca - 1);
      else this.envVca *= this.vcaDecA;
    } else this.envVca *= this.vcaRelA;
    // accent sweep: VCF env * VR7 through D27/R117 into C_ACC, bled by R_ACC
    const vAccIn = this.accentNote ? this.envVcf * this.accFrac : 0;
    const drive = vAccIn - c.V_D - this.vAcc;
    if (drive > 0) this.vAcc += drive * this.accChgK;
    this.vAcc *= this.accDisA;

    // --- VCF control: Millman sum at Q10 base, expo current
    const vEnvMod = this.accentNote ? 0 : this.envVcf * this.envModFrac;
    const vb = this.wCut * this.vCutPot + this.wEnv * vEnvMod + this.wAcc * this.vAcc;
    const Il = Math.min(c.I_REF * Math.exp(vb / VT), 2e-3);

    const y = this._vcfStep(wave * this.inAtt, Il);

    // --- output amp -> C14 coupling -> OTA VCA
    const ya = y * c.A_OUT; // Q21 diff stage -> Q19 output buffer
    this.hp14 = this.hp14A * (this.hp14 + ya - this.hp14In); this.hp14In = ya;
    const vo = this.hp14;
    // into the OTA: filter via C49/R88 plus resonance compensation (VR4 wiper) via C76/R100
    this.hp49 = this.hp49A * (this.hp49 + vo - this.hp49In); this.hp49In = vo;
    const vr = vo * this.knobs.resonance;
    this.hp76 = this.hp76A * (this.hp76 + vr - this.hp76In); this.hp76In = vr;
    const vin = (this.hp49 / c.R88 + this.hp76 / c.R100) / this.vinDen;
    const Iabc = Math.max(this.envVca * c.V_VCA - 0.6, 0) / c.R105 + Math.max(this.vAcc - 0.6, 0) / (c.R25 + c.R26);
    const iout = Iabc * Math.tanh(vin / (2 * VT));
    const vout = iout * c.R_OTA;
    this.hp38 = this.hp38A * (this.hp38 + vout - this.hp38In); this.hp38In = vout;
    const o = this.hp38 * this.vol * 4.0;
    return 0.95 * Math.tanh(o / 0.95); // output stage headroom
  }

  _decimate(x) {
    for (const s of this.decim) {
      const y = s.b0 * x + s.b1 * s.x1 + s.b2 * s.x2 - s.a1 * s.y1 - s.a2 * s.y2;
      s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y; x = y;
    }
    return x;
  }

  process(out, start = 0, end = out.length) {
    for (let n = start; n < end; n++) {
      let y = 0;
      for (let o = 0; o < this.os; o++) y = this._decimate(this._tick());
      out[n] = y;
    }
  }

  // diagnostics
  vcfResonantHz(vb) { // linear-theory peak for given base voltage
    const I = this.c.I_REF * Math.exp(vb / VT);
    return 1.3838 * (I / (4 * VT)) / (2 * Math.PI * this.c.C19);
  }
}

if (typeof module !== 'undefined' && module.exports) module.exports = { TB303Voice, COMPONENTS, dacVoltage, VT };
