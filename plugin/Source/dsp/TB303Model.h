/*
 * TB-303 component-level voice model, C++ port of tb303-model.js (the web app's engine).
 *
 * This is a line-by-line port: same parts table, same maths, same order of operations, so the plugin
 * sounds like the web app. tools/parity checks it against the JS sample for sample. When the JS model
 * changes, port the change here too.
 *
 * Header-only and free of JUCE, so the parity test can build it on its own.
 * Build it with -ffp-contract=off: fused multiply-adds would round differently from JavaScript.
 */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace tb303 {

constexpr double VT = 0.02585; // thermal voltage at ~300 K
constexpr double PI = 3.14159265358979323846;

// JavaScript's Math.round (halves go up)
inline double jsRound(double x) { return std::floor(x + 0.5); }
inline double clampd(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }
inline double audioTaper(double p) { return p <= 0 ? 0 : std::pow(p, 2.74); } // 'A' pot, ~15% at mid

// ---------------------------------------------------------------------------------------------------
// Parts table (COMPONENTS in the JS). Only what the engine needs: id, stock value, unit and dial range.
namespace P {
enum : int {
    VREF, R_DAC, R2_DAC, R93, C35, C33, V_LO, V_HI, F_TUNE, CV_TUNE, VR2, C10, R_SQB, SQ_LO, SQ_HI, R_SQC, R_SQE,
    C18, C19, C24, C26, R_IN, R98, C15, VR4, A_Q21, R73, R72, R63, R71, V_CUT, I_REF,
    C62, R138, VR6, R152, V_ENV, VR5, VR7, R117, C_ACC, R_ACC, V_D, WOW,
    C67, R49, R_SOFT, R52, R_VREL, V_VCA, R105, R25, R26, A_OUT, C14, R85, C49, R88, C76, R100, R101, R_OTA, C38, R_OUT,
    EN_VCF, EN_OTA, EN_OUT, NOISE,
    RAT_CIN, RAT_RD, RAT_CF, RAT_RG1, RAT_CG1, RAT_RG2, RAT_CG2, RAT_CC, RAT_GBW, RAT_SR, RAT_RCL, RAT_VF, RAT_RF, RAT_CFLT, RAT_VS,
    LEX_FS, LEX_BW, LEX_BITS, LEX_RANGE, LEX_GREL, LEX_WORD, LEX_MEM, LEX_XSAT,
    COUNT
};
}

struct PartInfo {
    const char* id; double v; const char* unit;
    double lo = 0, hi = 0; int mode = 0; // mode 0 = no explicit range, 1 = lin, 2 = log
};

inline const std::array<PartInfo, P::COUNT>& parts() {
    static const std::array<PartInfo, P::COUNT> t = {{
        { "VREF", 5.333, "V", 4, 6.5, 1 }, { "R_DAC", 100e3, "Ω" }, { "R2_DAC", 200e3, "Ω" },
        { "R93", 100e3, "Ω" }, { "C35", 0.22e-6, "F" },
        { "C33", 0.01e-6, "F" }, { "V_LO", 5.5, "V", 3, 8, 1 }, { "V_HI", 12.0, "V", 9, 15, 1 },
        { "F_TUNE", 110, "Hz", 55, 220, 2 }, { "CV_TUNE", 2.75, "V", 1, 4, 1 }, { "VR2", 50e3, "Ω" },
        { "C10", 0.01e-6, "F" }, { "R_SQB", 100e3, "Ω" }, { "SQ_LO", 5.0, "V" }, { "SQ_HI", 8.0, "V" },
        { "R_SQC", 10e3, "Ω" }, { "R_SQE", 1e3, "Ω" },
        { "C18", 0.018e-6, "F" }, { "C19", 0.033e-6, "F" }, { "C24", 0.033e-6, "F" }, { "C26", 0.033e-6, "F" },
        { "R_IN", 100e3, "Ω" }, { "R98", 2.2e3, "Ω" }, { "C15", 1e-6, "F" }, { "VR4", 50e3, "Ω" },
        { "A_Q21", 16.5, "×", 0, 26, 1 }, { "R73", 100e3, "Ω" }, { "R72", 220e3, "Ω" }, { "R63", 100e3, "Ω" },
        { "R71", 2.2e3, "Ω" }, { "V_CUT", 3.3, "V", 0.5, 6, 1 }, { "I_REF", 3.1e-6, "A", 1.55e-6, 6.2e-6, 2 },
        { "C62", 1e-6, "F" }, { "R138", 68e3, "Ω" }, { "VR6", 1e6, "Ω" }, { "R152", 100, "Ω" }, { "V_ENV", 5.0, "V" },
        { "VR5", 50e3, "Ω" }, { "VR7", 50e3, "Ω" }, { "R117", 22e3, "Ω" }, { "C_ACC", 1e-6, "F" }, { "R_ACC", 47e3, "Ω" },
        { "V_D", 0.55, "V" }, { "WOW", 0, "×", 0, 1, 1 },
        { "C67", 1e-6, "F" }, { "R49", 100, "Ω" }, { "R_SOFT", 3.3e3, "Ω" }, { "R52", 1.5e6, "Ω" }, { "R_VREL", 4.7e3, "Ω" },
        { "V_VCA", 5.0, "V" }, { "R105", 22e3, "Ω" }, { "R25", 22e3, "Ω" }, { "R26", 47e3, "Ω" }, { "A_OUT", 10, "×" },
        { "C14", 1e-6, "F" }, { "R85", 10e3, "Ω" }, { "C49", 0.01e-6, "F" }, { "R88", 220e3, "Ω" }, { "C76", 0.01e-6, "F" },
        { "R100", 100e3, "Ω" }, { "R101", 2.2e3, "Ω" }, { "R_OTA", 22e3, "Ω" }, { "C38", 1e-6, "F" }, { "R_OUT", 10e3, "Ω" },
        { "EN_VCF", 15e-9, "V/√Hz", 0, 150e-9, 1 }, { "EN_OTA", 40e-9, "V/√Hz", 0, 400e-9, 1 },
        { "EN_OUT", 0.35e-6, "V/√Hz", 0, 3.5e-6, 1 }, { "NOISE", 0.15, "×", 0, 0.15, 1 },
        { "RAT_CIN", 22e-9, "F" }, { "RAT_RD", 100e3, "Ω" }, { "RAT_CF", 100e-12, "F" }, { "RAT_RG1", 47, "Ω" },
        { "RAT_CG1", 2.2e-6, "F" }, { "RAT_RG2", 560, "Ω" }, { "RAT_CG2", 4.7e-6, "F" }, { "RAT_CC", 30e-12, "F" },
        { "RAT_GBW", 1e6, "Hz", 2e5, 5e6, 2 }, { "RAT_SR", 0.3, "V/µs", 0.05, 5, 2 }, { "RAT_RCL", 1e3, "Ω" },
        { "RAT_VF", 0.6, "V", 0.2, 2.5, 1 }, { "RAT_RF", 1.5e3, "Ω" }, { "RAT_CFLT", 3.3e-9, "F" }, { "RAT_VS", 9, "V", 6, 18, 1 },
        { "LEX_FS", 20000, "Hz", 10000, 48000, 2 }, { "LEX_BW", 9000, "Hz", 3000, 20000, 2 }, { "LEX_BITS", 12, "bit", 6, 20, 1 },
        { "LEX_RANGE", 24, "dB", 0, 42, 1 }, { "LEX_GREL", 0.08, "s", 0.005, 1, 2 }, { "LEX_WORD", 16, "bit", 8, 24, 1 },
        { "LEX_MEM", 32768, "words", 8192, 131072, 2 }, { "LEX_XSAT", 1.6, "V", 0.3, 6, 2 },
    }};
    return t;
}

inline int partIndex(const std::string& id) {
    for (int i = 0; i < P::COUNT; ++i) if (id == parts()[i].id) return i;
    return -1;
}

// the dial range the web app uses for a part (partRange in parts.js)
struct Range { double lo, hi; bool log; };
inline Range partRange(int i) {
    const auto& d = parts()[i];
    if (d.mode) return { d.lo, d.hi, d.mode == 2 };
    const std::string u = d.unit;
    if (u == "Ω" || u == "F" || u == "A") return { d.v / 10, d.v * 10, true };
    return { d.v * 0.5, d.v * 1.5, false };
}
inline double partFromT(int i, double t) {
    auto r = partRange(i); t = clampd(t, 0, 1);
    return r.log ? r.lo * std::pow(r.hi / r.lo, t) : r.lo + (r.hi - r.lo) * t;
}
inline double partToT(int i, double v) {
    auto r = partRange(i);
    double t = r.log ? std::log(v / r.lo) / std::log(r.hi / r.lo) : (v - r.lo) / (r.hi - r.lo);
    return clampd(t, 0, 1);
}

// ---------------------------------------------------------------------------------------------------
// Front panel knobs (0..1). Defaults are the web app's (KNOBS in state.js).
namespace K {
enum : int {
    tuning, cutoff, resonance, envmod, decay, accent, volume, dist, filter, ratvol,
    lexprog, lexbass, lexmid, lexxover, lextreble, lexdepth, lexdiff, lexpre, lexme, lexpitch, lexmix,
    cmpthr, cmprng, cmpatk, cmphold, cmprel, cmpmix, cmpmode,
    dlypp, dlytime, dlyfb, dlylo, dlyhi, dlymix,
    COUNT
};
}
struct KnobInfo { const char* id; const char* name; double def; const char* group; };
inline const std::array<KnobInfo, K::COUNT>& knobs() {
    static const std::array<KnobInfo, K::COUNT> t = {{
        { "tuning", "Tuning", 0.5, "voice" }, { "cutoff", "Cutoff", 0.3, "voice" }, { "resonance", "Reso", 0.82, "voice" },
        { "envmod", "Env mod", 0.62, "voice" }, { "decay", "Decay", 0.3, "voice" }, { "accent", "Accent", 0.75, "voice" },
        { "volume", "Volume", 0.85, "voice" },
        { "dist", "Distortion", 0.5, "rat" }, { "filter", "Filter", 0.4, "rat" }, { "ratvol", "Volume", 0.5, "rat" },
        { "lexprog", "Program", 0.125, "lex" }, { "lexbass", "Bass", 0.3, "lex" }, { "lexmid", "Mid", 0.27, "lex" },
        { "lexxover", "Crossover", 0.443, "lex" }, { "lextreble", "Treble", 0.834, "lex" }, { "lexdepth", "Depth", 0.5, "lex" },
        { "lexdiff", "Diffusion", 0.55, "lex" }, { "lexpre", "Predelay", 0.2, "lex" }, { "lexme", "Mode enh", 0.5, "lex" },
        { "lexpitch", "Pitch", 0.2, "lex" }, { "lexmix", "Return", 0.5, "lex" },
        { "cmpthr", "Threshold", 0.5, "cmp" }, { "cmprng", "Range", 0.5, "cmp" }, { "cmpatk", "Attack", 0.3, "cmp" },
        { "cmphold", "Hold", 0, "cmp" }, { "cmprel", "Release", 0.4, "cmp" }, { "cmpmix", "Mix", 0.7, "cmp" },
        { "cmpmode", "Mode", 0, "cmp" },
        { "dlypp", "Ping-pong", 1, "dly" }, { "dlytime", "Time", 0.7, "dly" }, { "dlyfb", "Feedback", 0.35, "dly" },
        { "dlylo", "Low cut", 0.3, "dly" }, { "dlyhi", "High cut", 0.55, "dly" }, { "dlymix", "Return", 0.35, "dly" },
    }};
    return t;
}
inline int knobIndex(const std::string& id) {
    for (int i = 0; i < K::COUNT; ++i) if (id == knobs()[i].id) return i;
    return -1;
}

using Knobs = std::array<double, K::COUNT>;
using Parts = std::array<double, P::COUNT>;

// 6-bit R-2R ladder solved by Thevenin reduction from the LSB end.
inline double dacVoltage(int code, double vref, double R, double R2) {
    double vth = 0, rth = R2;
    for (int k = 0; k < 6; k++) {
        const double vb = ((code >> k) & 1) ? vref : 0;
        vth = (vth / rth + vb / R2) / (1 / rth + 1 / R2);
        rth = (rth * R2) / (rth + R2);
        if (k < 5) rth += R;
    }
    return vth;
}

struct Biquad { double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0; };

// ---------------------------------------------------------------------------------------------------
// Lexicon 224 digital reverb (see the notes in tb303-model.js: the core is Dattorro's published plate,
// the other program variants are estimates).
struct LexProgram { int n; const char* name; const char* shortName; int alg; double size; double pre0, pre1; };
enum LexAlg { ALG_PLATE, ALG_HALLB, ALG_HALLA, ALG_CHAMBER, ALG_ROOM, ALG_CDPLATE, ALG_CHORUS };
inline const std::array<LexProgram, 9>& lexPrograms() {
    static const std::array<LexProgram, 9> t = {{
        { 1, "Small Concert Hall B", "Small Hall B", ALG_HALLB, 1.2, 24, 152 },
        { 2, "Vocal Plate", "Vocal Plate", ALG_PLATE, 1.0, 0, 107 },
        { 3, "Large Concert Hall B", "Large Hall B", ALG_HALLB, 1.85, 24, 152 },
        { 4, "Acoustic Chamber", "Chamber", ALG_CHAMBER, 1.1, 25, 255 },
        { 5, "Percussion Plate A", "Perc Plate", ALG_PLATE, 0.75, 0, 107 },
        { 6, "Small Concert Hall A", "Small Hall A", ALG_HALLA, 1.25, 24, 152 },
        { 7, "Room A", "Room A", ALG_ROOM, 0.5, 24, 255 },
        { 8, "Constant Density Plate A", "CD Plate", ALG_CDPLATE, 1.0, 5, 185 },
        { 9, "Chorus A", "Chorus", ALG_CHORUS, 1, 0, 253 },
    }};
    return t;
}
struct LexAlgShape { std::vector<int> ind; std::array<double, 8> mul; std::vector<int> ap3; double dd1, er; };
inline const LexAlgShape& lexAlg(int a) {
    static const LexAlgShape t[] = {
        { { 142, 107, 379, 277 }, { 1, 1, 1, 1, 1, 1, 1, 1 }, {}, 0.70, 0.25 },
        { { 142, 107, 379, 277 }, { 1.13, 1.07, 0.93, 1.11, 1.09, 0.97, 1.12, 1.04 }, { 1123, 1327 }, 0.65, 0.45 },
        { { 233, 571 }, { 1.21, 1.03, 1.07, 0.95, 0.89, 1.11, 0.97, 1.08 }, { 977, 1201 }, 0.60, 0.5 },
        { { 113, 163, 293 }, { 0.83, 0.91, 1.17, 0.88, 0.95, 0.86, 1.21, 0.93 }, {}, 0.68, 0.7 },
        { { 101, 271 }, { 1, 1, 1, 1, 1, 1, 1, 1 }, {}, 0.62, 0.9 },
        { { 142, 107, 379, 277, 199, 151 }, { 1, 0.97, 1, 1.03, 1, 1.02, 1, 0.98 }, { 811, 947 }, 0.75, 0.15 },
    };
    return t[a];
}
static const int LEX_TANK[8] = { 672, 4453, 1800, 3720, 908, 4217, 2656, 3163 };
static const int LEX_TAPS_L[7][3] = { { 5, 266, 1 }, { 5, 2974, 1 }, { 6, 1913, -1 }, { 7, 1996, 1 }, { 1, 1990, -1 }, { 2, 187, -1 }, { 3, 1066, -1 } };
static const int LEX_TAPS_R[7][3] = { { 1, 353, 1 }, { 1, 3627, 1 }, { 2, 1228, -1 }, { 3, 2673, 1 }, { 5, 2111, -1 }, { 6, 335, -1 }, { 7, 121, -1 } };
static const double LEX_CHORUS_MS[4] = { 9, 14, 19, 25 };

struct LexParams {
    int prog; double bass, mid, xover, treble; int depth, diff; double pre; int me, pitch; double pairs[4]; double wet;
};
inline LexParams lexParams(const Knobs& k) {
    auto ex = [](double lo, double hi, double p) { return lo * std::pow(hi / lo, p); };
    LexParams q;
    q.prog = (int)std::max(0.0, std::min(8.0, jsRound(k[K::lexprog] * 8)));
    const auto& Pr = lexPrograms()[q.prog];
    q.bass = ex(0.6, 70, k[K::lexbass]); q.mid = ex(0.6, 70, k[K::lexmid]);
    q.xover = ex(100, 10900, k[K::lexxover]); q.treble = ex(100, 10900, k[K::lextreble]);
    q.depth = (int)jsRound(k[K::lexdepth] * 71); q.diff = (int)jsRound(k[K::lexdiff] * 63);
    q.pre = Pr.pre0 + (Pr.pre1 - Pr.pre0) * k[K::lexpre];
    q.me = k[K::lexme] < 0.03 ? 0 : 17 - (int)std::max(1.0, std::min(16.0, std::ceil(k[K::lexme] * 16)));
    q.pitch = 1 + (int)jsRound(k[K::lexpitch] * 15);
    q.pairs[0] = k[K::lexbass]; q.pairs[1] = k[K::lexmid]; q.pairs[2] = k[K::lexxover]; q.pairs[3] = k[K::lextreble];
    q.wet = 2 * audioTaper(k[K::lexmix]);
    return q;
}

inline Biquad lexLowpass(double fc, double Q, double fs) {
    const double w = 2 * PI * std::min(fc, 0.45 * fs) / fs, al = std::sin(w) / (2 * Q), cw = std::cos(w), a0 = 1 + al;
    return { (1 - cw) / 2 / a0, (1 - cw) / a0, (1 - cw) / 2 / a0, -2 * cw / a0, (1 - al) / a0 };
}
inline double lexBiq(std::array<Biquad, 3>& f, double x) {
    for (auto& s : f) {
        const double y = s.b0 * x + s.b1 * s.x1 + s.b2 * s.x2 - s.a1 * s.y1 - s.a2 * s.y2;
        s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y; x = y;
    }
    return x;
}

class Lexicon224 {
public:
    explicit Lexicon224(double fsHost) : fsHost(fsHost) {}

    double L = 0, R = 0, wet = 0;

    void derive(const Parts& c, const Knobs& k) {
        const LexParams q = lexParams(k);
        const int F_ = (int)jsRound(c[P::LEX_FS]);
        const double fh = fsHost;
        const bool layoutChanged = F_ != F || (int)jsRound(c[P::LEX_MEM]) != memWords || q.prog != prog;
        if (F_ != F || c[P::LEX_BW] != bw) {
            F = F_; bw = c[P::LEX_BW]; ratio = F / fh;
            const double fc = std::min(c[P::LEX_BW], 0.45 * F), Qs[3] = { 0.5176, 0.7071, 1.9319 };
            for (int i = 0; i < 3; ++i) { aa[i] = lexLowpass(fc, Qs[i], fh); rcL[i] = lexLowpass(fc, Qs[i], fh); rcR[i] = lexLowpass(fc, Qs[i], fh); }
        }
        memWords = (int)jsRound(c[P::LEX_MEM]); prog = q.prog;
        if (layoutChanged) layout(1);
        adcQ = std::pow(2.0, jsRound(c[P::LEX_BITS]) - 1);
        steps = (int)std::max(0.0, jsRound(c[P::LEX_RANGE] / 6));
        envRel = std::exp(-1 / (c[P::LEX_GREL] * F));
        wq = std::pow(2.0, jsRound(c[P::LEX_WORD]) - 1);
        xsat = c[P::LEX_XSAT];
        hxA = std::exp(-2 * PI * 12 / fh);
        wet = q.wet;
        preD = (int)std::max(1.0, jsRound(q.pre * F / 1000));
        const double g1 = std::min(0.9, 0.75 * q.diff / 35);
        gIn[0] = g1; gIn[1] = g1; gIn[2] = g1 * 0.833; gIn[3] = g1 * 0.833; gIn[4] = g1 * 0.8; gIn[5] = g1 * 0.8;
        dd2 = std::min(0.7, 0.5 * q.diff / 35);
        if (alg != ALG_CHORUS) {
            auto half = [&](int o) { double s = 0; for (int i = 0; i < 4; i++) s += len[o + i]; return s; };
            const double hL = half(0) + (hasAp3 ? ap3[0] : 0), hR = half(4) + (hasAp3 ? ap3[1] : 0);
            auto g = [&](double rt, double h) { return std::pow(10.0, -3 * h / (rt * F)); };
            gB[0] = g(q.bass, hL); gB[1] = g(q.bass, hR); gM[0] = g(q.mid, hL); gM[1] = g(q.mid, hR);
        }
        cx = 1 - std::exp(-2 * PI * q.xover / F);
        tb = q.treble >= F / 2.0 ? 1 : 1 - std::exp(-2 * PI * q.treble / F);
        depth = q.depth / 71.0;
        meT = q.me ? q.me * 32 : 0;
        pitch = q.me ? q.pitch : 0;
        for (int i = 0; i < 4; ++i) pairs[i] = q.pairs[i];
        chDepth = q.depth / 71.0 * 0.008 * F;
        chT = (int)jsRound((q.me ? q.me : 8) * 0.04 * F);
    }

    // one host-rate sample in, sets L / R
    void run(double x) {
        hx = hxA * (hx + x - hxIn); hxIn = x;
        double v = xsat * std::tanh(hx / xsat);
        v = lexBiq(aa, v * 0.5);
        ph += ratio;
        while (ph >= 1) {
            ph -= 1;
            const double back = ph / ratio, xi = v - (v - prev) * std::min(1.0, back);
            const double a = std::fabs(xi); envIn = a > envIn ? a : envIn * envRel;
            tick(conv(xi, envIn)); const double yl_ = yl, yr_ = yr;
            const double al = std::fabs(yl_), ar = std::fabs(yr_);
            envL = al > envL ? al : envL * envRel; envR = ar > envR ? ar : envR * envRel;
            zl = conv(yl_, envL); zr = conv(yr_, envR);
        }
        prev = v;
        L = lexBiq(rcL, zl) * 2; R = lexBiq(rcR, zr) * 2;
    }

    struct MemoryUse { int words, of; double scale; bool chorus; };
    MemoryUse memoryUse() const { return { words, memWords, scale, alg == ALG_CHORUS }; }

    void flush() {
        auto f = [](double& x) { if (x != 0 && x < 1e-150 && x > -1e-150) x = 0; };
        f(hx); f(hxIn); f(prev); f(envIn); f(envL); f(envR); f(zl); f(zr); f(yl); f(yr); f(lastL); f(lastR); f(L); f(R);
        for (auto& d : xl) f(d); for (auto& d : tr) f(d);
        for (auto* bq : { &aa, &rcL, &rcR }) for (auto& s : *bq) { f(s.x1); f(s.x2); f(s.y1); f(s.y2); }
    }

private:
    struct Line { int b, len; };
    struct Walk { double from = 0, to = 0; };
    struct ChVoice { double from = 0, to = 0; int ctr = 1; };

    double fsHost;
    int F = 0, memWords = 0, prog = -1; double bw = -1, ratio = 1;
    uint32_t seed = 22477;
    double yl = 0, yr = 0, ph = 0, prev = 0, hx = 0, hxIn = 0, envIn = 0, envL = 0, envR = 0, zl = 0, zr = 0;
    std::array<Biquad, 3> aa {}, rcL {}, rcR {};
    double adcQ = 2048, envRel = 0, wq = 32768, xsat = 1.6, hxA = 0; int steps = 4;
    int preD = 1; double gIn[6] = {}; double dd2 = 0, dd1 = 0.7, er = 0.25;
    double gB[2] = {}, gM[2] = {}, cx = 0, tb = 1, depth = 0.5; int meT = 0, pitch = 0; double pairs[4] = {};
    double chDepth = 0; int chT = 1;
    int alg = ALG_PLATE;
    // layout
    int preB = 0, chB = 0, chLen = 0; std::vector<Line> chAp; std::array<ChVoice, 8> chW {};
    double scale = 1; std::vector<Line> ind; int len[8] = {}; int tbB[8] = {};
    bool hasAp3 = false; int ap3[2] = {}, ap3B[2] = {};
    int tapsL[7][3], tapsR[7][3], nearL[7][3], nearR[7][3];
    std::array<Walk, 4> walk {}; int meCtr = 1;
    double lastL = 0, lastR = 0, xl[2] = {}, tr[2] = {};
    int words = 0; std::vector<double> mem; int mask = 0; int p = 0;

    // JS: this.seed = (this.seed * 1103515245 + 12345) >>> 0 -- in doubles, so the product rounds first
    double rand() {
        const double prod = (double)seed * 1103515245.0;
        const double sum = prod + 12345.0;
        double m = std::fmod(std::trunc(sum), 4294967296.0); if (m < 0) m += 4294967296.0;
        seed = (uint32_t)m;
        return seed / 4294967296.0;
    }

    void layout(double fit) {
        const auto& Pr = lexPrograms()[prog];
        alg = Pr.alg;
        const int maxPre = (int)std::ceil(Pr.pre1 * F / 1000) + 2;
        words = 0;
        auto alloc = [&](int n) { const int b = words; words += n + 2; return b; };
        preB = alloc(maxPre);
        if (Pr.alg == ALG_CHORUS) {
            chLen = (int)std::ceil(0.045 * F);
            chB = alloc(chLen);
            static const double apS[4] = { 0.0053, 0.0071, 0.0047, 0.0083 };
            chAp.clear();
            for (int i = 0; i < 4; ++i) { const int l = (int)jsRound(apS[i] * F); chAp.push_back({ alloc(l), l }); }
            for (auto& w : chW) { w.from = 0; w.to = 0; w.ctr = 1 + (int)std::floor(rand() * 2000); }
            scale = 1;
        } else {
            const auto& A = lexAlg(Pr.alg);
            double base = 0;
            for (int n : A.ind) base += n;
            { double s = 0; for (int i = 0; i < 8; ++i) s = s + LEX_TANK[i] * A.mul[i]; base = base + s; }
            if (!A.ap3.empty()) base = base + (A.ap3[0] + A.ap3[1]);
            double sc = F / 29761.0 * Pr.size * fit;
            const double room = memWords - maxPre - 3 * ((int)A.ind.size() + 12) - 80;
            if (base * sc > room) sc = std::max(0.05, room / base);
            scale = sc / (F / 29761.0);
            auto Lf = [&](double n) { return (int)std::max(2.0, jsRound(n * sc)) | 1; };
            ind.clear();
            for (int n : A.ind) { const int l = Lf(n); ind.push_back({ alloc(l), l }); }
            for (int i = 0; i < 8; ++i) len[i] = Lf(LEX_TANK[i] * A.mul[i]);
            for (int i = 0; i < 8; ++i) tbB[i] = alloc(len[i] + ((i & 3) <= 1 ? 36 : 0));
            hasAp3 = !A.ap3.empty();
            if (hasAp3) { ap3[0] = Lf(A.ap3[0]); ap3[1] = Lf(A.ap3[1]); ap3B[0] = alloc(ap3[0]); ap3B[1] = alloc(ap3[1]); }
            dd1 = A.dd1; er = A.er;
            auto tap = [&](const int t[3], bool near, int out[3]) {
                out[0] = t[0];
                out[1] = (int)std::max(1.0, jsRound((double)t[1] / LEX_TANK[t[0]] * len[t[0]] * (near ? 0.3 : 1)));
                out[2] = t[2];
            };
            for (int i = 0; i < 7; ++i) {
                tap(LEX_TAPS_L[i], false, tapsL[i]); tap(LEX_TAPS_R[i], false, tapsR[i]);
                tap(LEX_TAPS_L[i], true, nearL[i]); tap(LEX_TAPS_R[i], true, nearR[i]);
            }
            for (auto& w : walk) { w.from = 0; w.to = 0; }
            meCtr = 1;
            lastL = 0; lastR = 0; xl[0] = xl[1] = 0; tr[0] = tr[1] = 0;
        }
        if (words > memWords && fit > 0.5 && Pr.alg != ALG_CHORUS) { layout(fit * (memWords - 8) / words); return; }
        int size = 1024; while (size < words) size <<= 1;
        mem.assign((size_t)size, 0.0); mask = size - 1; p = 0;
    }

    void w(int a, double v) {
        v = std::trunc(v * wq) / wq;
        mem[(size_t)(a & mask)] = v > 1 ? 1 : v < -1 ? -1 : v;
    }
    double r(int a) const { return mem[(size_t)(a & mask)]; }
    double rf(int a, double d) const { const double fl = std::floor(d); const int i = (int)fl; const double f = d - fl; return r(a + i) * (1 - f) + r(a + i + 1) * f; }
    double ap(int b, int l, double g, double x) { const double vd = r(p + b + l), ww = x + g * vd; w(p + b, ww); return vd - g * ww; }

    double conv(double x, double env) const {
        int s = steps; while (s > 0 && env * (double)(1 << s) >= 1) s--;
        const double g = (double)(1 << s); double y = x * g; y = y > 1 ? 1 : y < -1 ? -1 : y;
        return jsRound(y * adcQ) / adcQ / g;
    }

    double sum(const int T[7][3], int pp) const {
        double y = 0;
        for (int i = 0; i < 7; ++i) y += T[i][2] * r(pp + tbB[T[i][0]] + T[i][1]);
        return y;
    }

    void tick(double x) {
        p = (p - 1) & mask;
        w(p + preB, x);
        const double u0 = r(p + preB + preD);
        if (alg == ALG_CHORUS) { chorus(u0); return; }
        double u = u0, uE = 0;
        const int ni = (int)ind.size();
        for (int i = 0; i < ni; i++) { u = ap(ind[i].b, ind[i].len, gIn[i], u); if (i == ni - 2) uE = u; }
        if (meT) {
            if (--meCtr <= 0) {
                meCtr = meT;
                for (auto& wk : walk) {
                    wk.from = wk.to; double n = wk.to + (rand() < 0.5 ? -1 : 1);
                    if (std::fabs(n) > pitch) { const double d = n - wk.to; n = wk.to - (d > 0 ? 1 : d < 0 ? -1 : 0); }
                    wk.to = n;
                }
            }
        } else { meCtr = 1; for (auto& wk : walk) { wk.from = wk.to = 0; } }
        const double ph_ = meT ? 1 - (double)meCtr / meT : 0;
        double out0 = 0, out1 = 0;
        for (int s = 0; s < 2; s++) {
            const int o = s * 4;
            double a = u + (s ? lastL : lastR);
            const double vd = rf(p + tbB[o], len[o] + 16 + (walk[s].from + (walk[s].to - walk[s].from) * ph_)), g = -dd1, ww = a + g * vd;
            w(p + tbB[o], ww); a = vd - g * ww;
            w(p + tbB[o + 1], a);
            double v = rf(p + tbB[o + 1], len[o + 1] + 16 + (walk[2 + s].from + (walk[2 + s].to - walk[2 + s].from) * ph_));
            xl[s] += cx * (v - xl[s]);
            v = gB[s] * xl[s] + gM[s] * (v - xl[s]);
            tr[s] += tb * (v - tr[s]); v = tr[s];
            v = ap(tbB[o + 2], len[o + 2], dd2, v);
            if (hasAp3) v = ap(ap3B[s], ap3[s], -dd2 * 0.8, v);
            w(p + tbB[o + 3], v);
            if (s) out1 = r(p + tbB[o + 3] + len[o + 3]); else out0 = r(p + tbB[o + 3] + len[o + 3]);
        }
        lastL = out0; lastR = out1;
        const double d = depth;
        yl = 0.6 * (d * sum(tapsL, p) + (1 - d) * (sum(nearL, p) + er * u));
        yr = 0.6 * (d * sum(tapsR, p) + (1 - d) * (sum(nearR, p) + er * uE));
    }

    void chorus(double u) {
        w(p + chB, u);
        double y[2] = { 0, 0 };
        for (int v = 0; v < 8; v++) {
            auto& cw = chW[v];
            if (--cw.ctr <= 0) { cw.from = cw.to; cw.to = rand() * 2 - 1; cw.ctr = chT; }
            const double t = 1 - (double)cw.ctr / chT, sm = cw.from + (cw.to - cw.from) * (0.5 - 0.5 * std::cos(PI * t));
            const int pair = v >> 1, side = v & 1;
            const double dl = std::min((double)(chLen - 2), (LEX_CHORUS_MS[pair] + side * 1.3) * 0.001 * F + chDepth * sm);
            double s = rf(p + chB, std::max(1.0, dl));
            if (pair >= 2) { const auto& a = chAp[(pair - 2) * 2 + side]; s = ap(a.b, a.len, dd2 * 1.2, s); }
            y[side] += pairs[pair] * s;
        }
        yl = 0.5 * y[0]; yr = 0.5 * y[1];
    }
};

// ---------------------------------------------------------------------------------------------------
// Simple digital delay, ping-pong by default.
static const double DLY_DIVS[11] = { 1.0 / 8, 1.0 / 6, 1.0 / 4, 1.0 / 3, 3.0 / 8, 1.0 / 2, 2.0 / 3, 3.0 / 4, 1, 3.0 / 2, 2 };
static const char* const DLY_NAMES[11] = { "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/4D", "1/2" };
inline int dlyDivIndex(double p) { return (int)std::min(10.0, jsRound(p * 10)); }
inline double dlyLo(double p) { return 20 * std::pow(50.0, p); }
inline double dlyHi(double p) { return 1000 * std::pow(20.0, p); }

class DigitalDelay {
public:
    explicit DigitalDelay(double fs) : fs(fs) {
        buf.assign((size_t)std::ceil(fs * 2.6), 0.0f); bufR.assign(buf.size(), 0.0f);
        d = fs * 0.25; dT = d;
    }
    double l = 0, r = 0;
    void derive(const Knobs& k, double bpm) {
        dT = std::min((double)buf.size() - 4, DLY_DIVS[dlyDivIndex(k[K::dlytime])] * 60 / bpm * fs);
        fb = 0.95 * k[K::dlyfb];
        lpA = std::exp(-2 * PI * std::min(dlyHi(k[K::dlyhi]), 0.45 * fs) / fs);
        hpA = std::exp(-2 * PI * dlyLo(k[K::dlylo]) / fs);
        mix = k[K::dlymix] * k[K::dlymix] * 1.2;
        slewA = 1 - std::exp(-1 / (0.06 * fs));
        const bool pp_ = k[K::dlypp] >= 0.5; if (pp_ != pp) { pp = pp_; std::fill(bufR.begin(), bufR.end(), 0.0f); }
    }
    double run(double x) {
        float* b = buf.data(); float* B = bufR.data(); const int N = (int)buf.size();
        d += (dT - d) * slewA;
        double rr = w - d; if (rr < 0) rr += N;
        const double fl = std::floor(rr); const int i = (int)fl; const double f = rr - fl; const int j = i + 1 == N ? 0 : i + 1;
        double y = b[i] + ((double)b[j] - b[i]) * f;
        hp = hpA * (hp + y - hpIn); hpIn = y; y = hp;
        lp = y + lpA * (lp - y); y = lp;
        if (pp) {
            double z = B[i] + ((double)B[j] - B[i]) * f;
            hpR = hpA * (hpR + z - hpInR); hpInR = z; z = hpR;
            lpR = z + lpA * (lpR - z); z = lpR;
            b[w] = (float)(x + fb * z); B[w] = (float)y;
            l = y * mix; r = z * mix;
        } else {
            b[w] = (float)(x + fb * y);
            l = r = y * mix;
        }
        w = w + 1 == N ? 0 : w + 1;
        return l;
    }
    void flush() { auto f = [](double& x) { if (x != 0 && x < 1e-150 && x > -1e-150) x = 0; }; f(lp); f(hp); f(hpIn); f(lpR); f(hpR); f(hpInR); f(l); f(r); }
private:
    double fs; std::vector<float> buf, bufR; int w = 0;
    double d, dT, lp = 0, hp = 0, hpIn = 0, lpR = 0, hpR = 0, hpInR = 0; bool pp = true;
    double fb = 0, lpA = 0, hpA = 0, mix = 0, slewA = 0;
};

// ---------------------------------------------------------------------------------------------------
// Sidechain ducker on the effect returns, after the Kilohearts Compactor (our reading of its controls).
static const char* const CMP_MODES[3] = { "Peak", "ISP", "RMS" };
inline double cmpThrDb(double p) { return -40 + 40 * p; }
inline double cmpAtkMs(double p) { return 10 * p; }
inline double cmpHoldMs(double p) { return 500 * p * p; }
inline double cmpRelMs(double p) { return 1000 * p * p; }

class Compactor {
public:
    explicit Compactor(double fs) : fs(fs) { L.assign((size_t)std::ceil(fs * 0.011), 0.0f); R.assign(L.size(), 0.0f); }
    double l = 0, r = 0, g = 1;
    void derive(const Knobs& k) {
        thr = std::pow(10.0, cmpThrDb(k[K::cmpthr]) / 20);
        rng = 2 * k[K::cmprng];
        D = (int)std::min((double)L.size() - 1, jsRound(cmpAtkMs(k[K::cmpatk]) * 1e-3 * fs));
        atkA = D > 0 ? 1 - std::exp(-3.0 / D) : 1;
        holdN = (int)jsRound(cmpHoldMs(k[K::cmphold]) * 1e-3 * fs);
        const double rel = cmpRelMs(k[K::cmprel]) * 1e-3 * fs;
        relA = rel > 0.5 ? std::exp(-1 / rel) : 0;
        mode = (int)jsRound(k[K::cmpmode] * 2);
        rmsA = std::exp(-1 / (0.01 * fs));
        mix = k[K::cmpmix];
    }
    double run(double y, double wl, double wr) {
        double det;
        if (mode == 2) { ms = y * y + rmsA * (ms - y * y); det = std::sqrt(ms * 2); }
        else {
            det = std::fabs(y);
            if (mode == 1) { const double m = (-k3 + 9 * k2 + 9 * k1 - y) / 16; if (std::fabs(m) > det) det = std::fabs(m); }
            k3 = k2; k2 = k1; k1 = y;
        }
        const double a = amount(det);
        if (a >= env) { env += (a - env) * atkA; hold = holdN; }
        else if (hold > 0) hold--;
        else env = a + (env - a) * relA;
        const int N = (int)L.size(); int ri = w - D; if (ri < 0) ri += N;
        L[(size_t)w] = (float)wl; R[(size_t)w] = (float)wr; l = L[(size_t)ri]; r = R[(size_t)ri]; w = w + 1 == N ? 0 : w + 1;
        g = 1 - mix * env;
        return g;
    }
    void flush() { auto f = [](double& x) { if (x != 0 && x < 1e-150 && x > -1e-150) x = 0; }; f(env); f(ms); f(k1); f(k2); f(k3); f(l); f(r); }
private:
    double amount(double det) const {
        const double x = det / thr, Rr = rng;
        double a;
        if (Rr <= 1) a = Rr > 0 ? (x - (1 - Rr)) / Rr : 0;
        else { const double s = Rr - 1; a = (1 - s) * x + s * std::tanh(3 * x) / 0.99505; }
        return a <= 0 ? 0 : a >= 1 ? 1 : a;
    }
    double fs; std::vector<float> L, R; int w = 0;
    double env = 0; int hold = 0; double ms = 0, k1 = 0, k2 = 0, k3 = 0;
    double thr = 0.1, rng = 1; int D = 0; double atkA = 1; int holdN = 0; double relA = 0; int mode = 0; double rmsA = 0, mix = 0.7;
};

// ---------------------------------------------------------------------------------------------------
constexpr int RAT_OS = 2; // the RAT runs at 2x the voice rate (4x the host rate)

class Voice {
public:
    Voice(double sampleRate, uint32_t noiseSeed = 0x9e3779b9u)
        : fsOut(sampleRate), fs(sampleRate * 2), h(1 / (sampleRate * 2)), lex(sampleRate), dly(sampleRate), cmp(sampleRate) {
        for (int i = 0; i < P::COUNT; ++i) c[i] = parts()[i].v;
        // the JS voice's own constructor defaults; the app (and the plugin) then sends the panel values
        const double kd[K::COUNT] = { 0.5, 0.35, 0.7, 0.6, 0.3, 0.7, 0.8, 0.5, 0.4, 0.5,
            0.125, 0.3, 0.27, 0.443, 0.834, 0.5, 0.55, 0.2, 0.5, 0.2, 0.5,
            0.5, 0.5, 0.3, 0, 0.4, 0.7, 0,
            1, 0.7, 0.35, 0.3, 0.55, 0.35 };
        for (int i = 0; i < K::COUNT; ++i) knobs[i] = kd[i];
        rng = noiseSeed;
        cvTarget = dacVoltage(33, c[P::VREF], c[P::R_DAC], c[P::R2_DAC]);
        cv = cvTarget;
        vSaw = c[P::V_LO]; sawDelay = vSaw; sqPrevIn = c[P::V_LO];
        makeDecimator();
        derive();
    }

    Parts c {};
    Knobs knobs {};
    bool fxRat = false, fxLex = false, fxDly = false;
    bool square = false;
    double bpm = 128;
    Lexicon224 lex; DigitalDelay dly; Compactor cmp;
    bool gate = false;

    void setComponent(int i, double v) { c[i] = v; derive(); }
    void setKnob(int i, double v) { knobs[i] = v; derive(); }
    // microtuning: cents for the 12 notes (C first, every octave), held as volts on the 1 V/oct CV; the next note picks it up
    double tuneCv[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    void setTune(const double* cents) { for (int i = 0; i < 12; i++) tuneCv[i] = cents[i] / 1200; }

    void derive() {
        const Knobs& k = knobs;
        cmp.derive(k); dly.derive(k, bpm);
        const double nb = std::sqrt(fs / 2) * c[P::NOISE];
        nVcf = c[P::EN_VCF] * nb; nOta = c[P::EN_OTA] * nb; nOut = c[P::EN_OUT] * nb;
        noiseOn = nVcf + nOta + nOut > 0;
        swing = c[P::V_HI] - c[P::V_LO];
        iVcoRef = c[P::F_TUNE] * c[P::C33] * swing;
        tuneOct = (k[K::tuning] - 0.5) * 2;
        sqA = std::exp(-1 / (c[P::C10] * c[P::R_SQB] * fs));
        sqGain = c[P::R_SQC] / c[P::R_SQE];
        sqLpA = std::exp(-2 * PI * 18000 / fs);
        slideA = std::exp(-1 / (c[P::R93] * c[P::C35] * fs));
        C[0] = c[P::C18]; C[1] = c[P::C19]; C[2] = c[P::C24]; C[3] = c[P::C26];
        inAtt = c[P::R98] / (c[P::R_IN] + c[P::R98]);
        kFb = c[P::A_Q21] * k[K::resonance];
        fbA = std::exp(-1 / (c[P::C15] * c[P::R98] * fs));
        const double g73 = 1 / c[P::R73], g72 = 1 / c[P::R72], g63 = 1 / c[P::R63], g71 = 1 / c[P::R71];
        const double gs = g73 + g72 + g63 + g71;
        wCut = g73 / gs; wEnv = g72 / gs; wAcc = g63 / gs;
        vCutPot = c[P::V_CUT] * k[K::cutoff];
        envModFrac = audioTaper(k[K::envmod]);
        const double rDecay = c[P::R138] + c[P::VR6] * audioTaper(k[K::decay]);
        envDecA = std::exp(-1 / (rDecay * c[P::C62] * fs));
        envDecAccA = std::exp(-1 / (c[P::R138] * c[P::C62] * fs));
        envAtkA = std::exp(-1 / (c[P::R152] * c[P::C62] * fs));
        vcaAtkA = std::exp(-1 / (c[P::R49] * c[P::C67] * fs));
        vcaSoftA = std::exp(-1 / ((c[P::R49] + c[P::R_SOFT]) * c[P::C67] * fs));
        vcaDecA = std::exp(-1 / (c[P::R52] * c[P::C67] * fs));
        vcaRelA = std::exp(-1 / (c[P::R_VREL] * c[P::C67] * fs));
        accFrac = k[K::accent];
        accChgK = 1 / (c[P::R117] * c[P::C_ACC] * fs);
        accDisA = std::exp(-1 / (c[P::R_ACC] * c[P::C_ACC] * fs));
        {
            const double ra = std::max(c[P::VR4] * k[K::resonance], 1.0), rb = std::max(c[P::VR4] * (1 - k[K::resonance]), 1.0);
            const double rs = c[P::R_ACC] + c[P::VR7] * k[K::accent] * (1 - k[K::accent]);
            const double gl = 1 / (c[P::R63] + 1 / (g73 + g72 + g71)), gb = 1 / rb, dt = 1 / fs;
            auto tap = [&](double gsrc) { const double sum = gsrc + gb + gl; return WowTap { gsrc / sum, gb / sum, gsrc / (gsrc + gl), std::exp(-dt * gb * (gsrc + gl) / sum / c[P::C_ACC]) }; };
            wowOn = tap(1 / (rs + ra)); wowOff = tap(0);
        }
        hp14A = std::exp(-1 / (c[P::C14] * c[P::R85] * fs));
        hp49A = std::exp(-1 / (c[P::C49] * c[P::R88] * fs));
        hp76A = std::exp(-1 / (c[P::C76] * c[P::R100] * fs));
        hp38A = std::exp(-1 / (c[P::C38] * c[P::R_OUT] * fs));
        vinDen = 1 / c[P::R88] + 1 / c[P::R100] + 1 / c[P::R101];
        vol = audioTaper(k[K::volume]);
        deriveFx();
    }

    // note: 0..63 DAC code (33 = A, 110 Hz)
    void noteOn(int code, bool accent, bool slide_) {
        code = std::max(0, std::min(63, code));
        cvTarget = dacVoltage(code, c[P::VREF], c[P::R_DAC], c[P::R2_DAC]) + tuneCv[code % 12];
        const bool legato = gate && slide_;
        slide = slide_;
        if (!legato) {
            if (!slide) cv = cvTarget;
            accentNote = accent;
            envAttack = 1;
            vcaAttack = 1;
        }
        gate = true;
    }
    void noteOff() { gate = false; }

    // the effects after the dry signal y
    void fxOut(double y, float* out, float* outR, int n) {
        double wl = 0, wr = 0;
        if (fxLex) { lex.run(y); wl += lex.wet * lex.L; wr += lex.wet * lex.R; }
        if (fxDly) { dly.run(y); wl += dly.l; wr += dly.r; }
        const double g = cmp.run(y, wl, wr);
        out[n] = (float)(0.98 * std::tanh((y + cmp.l * g) / 0.98));
        if (outR) outR[n] = (float)(0.98 * std::tanh((y + cmp.r * g) / 0.98));
    }

    void flush() {
        auto f = [](double& x) { if (x != 0 && x < 1e-150 && x > -1e-150) x = 0; };
        for (auto& x : v) f(x);
        f(fbLp); f(envVcf); f(envVca); f(vAcc); f(vWowC); f(vWow); f(hp14); f(hp14In); f(hp38); f(hp38In); f(hp49); f(hp49In); f(hp76); f(hp76In);
        f(sqHp); f(sqLp); f(rt.hin); f(rt.xin); f(rt.h1); f(rt.h2); f(rt.x1); f(rt.fb); f(rt.y); f(rt.hc); f(rt.xc); f(rt.v); f(rt.f); f(rt.xp); f(rt.dco); f(rt.dci);
        for (auto& s : decim) { f(s.x1); f(s.x2); f(s.y1); f(s.y2); }
        for (auto& s : ratLP) { f(s.x1); f(s.x2); f(s.y1); f(s.y2); }
        if (fxLex) lex.flush(); dly.flush(); cmp.flush();
    }

    // out is mono, or the left channel when outR is given
    void process(float* out, int start, int end, float* outR) {
        flush();
        const bool fx = fxLex || fxDly;
        for (int n = start; n < end; n++) {
            double y = 0;
            for (int o = 0; o < 2; o++) y = decimate(tick());
            if (fx) fxOut(y, out, outR, n);
            else { out[n] = (float)y; if (outR) outR[n] = (float)y; }
        }
    }

private:
    struct WowTap { double a, b, tgt, k; };
    struct RatState { double hin = 0, xin = 0, h1 = 0, h2 = 0, x1 = 0, fb = 0, y = 0, hc = 0, xc = 0, v = 0, f = 0, xp = 0, dco = 0, dci = 0; };

    double fsOut, fs, h;
    RatState rt;
    double cvTarget, cv; bool slide = false, accentNote = false;
    double vSaw, sawDelay, sqHp = 0, sqPrevIn, sqLp = 0;
    double v[4] = { 0, 0, 0, 0 }; double fbLp = 0;
    double envVcf = 0; int envAttack = 0; double envVca = 0; int vcaAttack = 0;
    double vAcc = 0, vWowC = 0, vWow = 0;
    uint32_t rng;
    double hp14 = 0, hp14In = 0, hp38 = 0, hp38In = 0, hp49 = 0, hp49In = 0, hp76 = 0, hp76In = 0;
    std::array<Biquad, 2> decim {};
    bool ratLPMade = false; std::array<Biquad, 3> ratLP {};
    // derived
    double nVcf = 0, nOta = 0, nOut = 0; bool noiseOn = false;
    double swing = 0, iVcoRef = 0, tuneOct = 0, sqA = 0, sqGain = 0, sqLpA = 0, slideA = 0;
    double C[4] = {}; double inAtt = 0, kFb = 0, fbA = 0, wCut = 0, wEnv = 0, wAcc = 0, vCutPot = 0, envModFrac = 0;
    double envDecA = 0, envDecAccA = 0, envAtkA = 0, vcaAtkA = 0, vcaSoftA = 0, vcaDecA = 0, vcaRelA = 0;
    double accFrac = 0, accChgK = 0, accDisA = 0; WowTap wowOn {}, wowOff {};
    double hp14A = 0, hp49A = 0, hp76A = 0, hp38A = 0, vinDen = 0, vol = 0;
    double ratDcA = 0, ratRd = 0, ratInA = 0, ratA1 = 0, ratA2 = 0, ratFA = 0, ratGain = 0, ratK = 0, ratSlew = 0, ratRail = 0, ratCA = 0, ratN = 0, ratIs = 0, ratFltA = 0, ratLevel = 0;

    void deriveFx() {
        const double fsr = fs * RAT_OS;
        auto e = [&](double tau) { return std::exp(-1 / (std::max(tau, 1e-12) * fsr)); };
        if (!ratLPMade) {
            ratLPMade = true;
            const double fc = 22000 / fsr, w = 2 * PI * fc, cs = std::cos(w), Qs[3] = { 0.5176, 0.7071, 1.9319 };
            for (int i = 0; i < 3; ++i) { const double al = std::sin(w) / (2 * Qs[i]), a0 = 1 + al; ratLP[i] = { (1 - cs) / 2 / a0, (1 - cs) / a0, (1 - cs) / 2 / a0, -2 * cs / a0, (1 - al) / a0 }; }
        }
        ratDcA = std::exp(-2 * PI * 5 / fs);
        const double rd = c[P::RAT_RD] * audioTaper(knobs[K::dist]) + 1;
        ratRd = rd;
        ratInA = e(c[P::RAT_CIN] * 1e6);
        ratA1 = e(c[P::RAT_RG1] * c[P::RAT_CG1]); ratA2 = e(c[P::RAT_RG2] * c[P::RAT_CG2]);
        ratFA = e(rd * c[P::RAT_CF]);
        const double comp = 30e-12 / c[P::RAT_CC];
        const double gHf = 1 + rd * (1 / c[P::RAT_RG1] + 1 / c[P::RAT_RG2]);
        ratGain = gHf;
        ratK = std::min(1.0, 2 * PI * c[P::RAT_GBW] * comp / gHf / fsr);
        ratSlew = c[P::RAT_SR] * 1e6 * comp / fsr;
        ratRail = std::max(c[P::RAT_VS] / 2 - 1, 0.5);
        ratCA = e(4.7e-6 * (c[P::RAT_RCL] + 1e3));
        ratN = 1.8 * VT;
        ratIs = 1e-3 / std::exp(c[P::RAT_VF] / ratN);
        ratFltA = e((c[P::RAT_RF] + 100e3 * knobs[K::filter]) * c[P::RAT_CFLT]);
        ratLevel = audioTaper(knobs[K::ratvol]) * 6;
        lex.derive(c, knobs);
    }

    double rat(double x) {
        RatState& s = rt; const double x0 = s.xp; s.xp = x;
        double y = 0;
        for (int i = 1; i <= RAT_OS; i++) {
            double z = ratCore(x0 + (x - x0) * ((double)i / RAT_OS));
            for (auto& q : ratLP) { const double o = q.b0 * z + q.b1 * q.x1 + q.b2 * q.x2 - q.a1 * q.y1 - q.a2 * q.y2; q.x2 = q.x1; q.x1 = z; q.y2 = q.y1; q.y1 = o; z = o; }
            y = z;
        }
        s.dco = ratDcA * (s.dco + y - s.dci); s.dci = y;
        return s.dco;
    }

    double ratCore(double x) {
        RatState& s = rt;
        s.hin = ratInA * (s.hin + x - s.xin); s.xin = x;
        const double u = s.hin;
        s.h1 = ratA1 * (s.h1 + u - s.x1); s.h2 = ratA2 * (s.h2 + u - s.x1); s.x1 = u;
        const double iv = (s.h1 / c[P::RAT_RG1] + s.h2 / c[P::RAT_RG2]) * ratRd;
        s.fb = iv + ratFA * (s.fb - iv);
        const double target = u + s.fb;
        double dy = (target - s.y) * ratK;
        if (dy > ratSlew) dy = ratSlew; else if (dy < -ratSlew) dy = -ratSlew;
        s.y = std::max(-ratRail, std::min(ratRail, s.y + dy));
        s.hc = ratCA * (s.hc + s.y - s.xc); s.xc = s.y;
        const double w = s.hc, R = c[P::RAT_RCL], n = ratN, Is = ratIs;
        double lo = w < 0 ? w : 0, hi = w < 0 ? 0 : w;
        double vv = std::min(hi, std::max(lo, s.v));
        for (int i = 0; i < 40; i++) {
            const double t = std::min(std::max(vv / n, -80.0), 80.0), ee = std::exp(t), ei = 1 / ee;
            const double f = (w - vv) / R - Is * (ee - ei), df = -1 / R - Is * (ee + ei) / n;
            if (f > 0) lo = vv; else hi = vv;
            double nv = vv - f / df;
            if (!(nv > lo && nv < hi)) nv = 0.5 * (lo + hi);
            const double dv = nv - vv; vv = nv;
            if (dv < 1e-7 && dv > -1e-7) break;
        }
        s.v = vv;
        s.f = vv + ratFltA * (s.f - vv);
        return s.f * ratLevel;
    }

    void makeDecimator() {
        auto mk = [&](double Q) {
            const double w = 2 * PI * 0.42 * fsOut / fs;
            const double al = std::sin(w) / (2 * Q), cs = std::cos(w), a0 = 1 + al;
            return Biquad { (1 - cs) / 2 / a0, (1 - cs) / a0, (1 - cs) / 2 / a0, -2 * cs / a0, (1 - al) / a0 };
        };
        decim[0] = mk(0.5412); decim[1] = mk(1.3066);
    }

public:
    // diagnostics (readouts)
    double vcfResonantHz(double vb) const { const double I = c[P::I_REF] * std::exp(vb / VT); return 1.3838 * (I / (4 * VT)) / (2 * PI * c[P::C19]); }
    double cutoffBase() const { return wCut * vCutPot; }
    double envTopBase() const { return wCut * vCutPot + wEnv * c[P::V_ENV] * envModFrac; }
private:
    double vcfStep(double u, double Iladder) {
        const double h2 = h / 2, twoVt = 2 * VT, Ih = Iladder / 2, G0 = Iladder / (4 * VT);
        const double k = kFb;
        const double uEff = u + k * fbLp;
        const double v0 = v[0], v1 = v[1], v2 = v[2], v3 = v[3];
        const double b0 = uEff - k * v3, b1 = v0 - v1, b2 = v1 - v2, b3 = v2 - v3, b4 = v3;
        const double T0 = Ih * std::tanh(b0 / twoVt), T1 = Ih * std::tanh(b1 / twoVt), T2 = Ih * std::tanh(b2 / twoVt), T3 = Ih * std::tanh(b3 / twoVt), T4 = Ih * std::tanh(b4 / twoVt);
        const double G0_ = std::fabs(b0) > 1e-9 ? T0 / b0 : G0, G1 = std::fabs(b1) > 1e-9 ? T1 / b1 : G0, G2 = std::fabs(b2) > 1e-9 ? T2 / b2 : G0,
                     G3 = std::fabs(b3) > 1e-9 ? T3 / b3 : G0, G4 = std::fabs(b4) > 1e-9 ? T4 / b4 : G0;
        const double C0 = C[0], C1 = C[1], C2 = C[2], C3 = C[3];
        double a00 = 1 + h2 * G1 / C0, a01 = -h2 * G1 / C0, a02 = 0, a03 = h2 * G0_ * k / C0;
        double a10 = -h2 * G1 / C1, a11 = 1 + h2 * (G1 + G2) / C1, a12 = -h2 * G2 / C1, a13 = 0;
        double a21 = -h2 * G2 / C2, a22 = 1 + h2 * (G2 + G3) / C2, a23 = -h2 * G3 / C2;
        double a32 = -h2 * G3 / C3, a33 = 1 + h2 * (G3 + G4) / C3;
        double r0 = v0 + h2 * (T0 - T1) / C0 + h2 * (G0_ / C0) * uEff, r1 = v1 + h2 * (T1 - T2) / C1, r2 = v2 + h2 * (T2 - T3) / C2, r3 = v3 + h2 * (T3 - T4) / C3;
        double m = a10 / a00;
        if (m != 0) { a11 -= m * a01; a12 -= m * a02; a13 -= m * a03; r1 -= m * r0; }
        m = a21 / a11;
        if (m != 0) { a22 -= m * a12; a23 -= m * a13; r2 -= m * r1; }
        m = a32 / a22;
        if (m != 0) { a33 -= m * a23; r3 -= m * r2; }
        const double n3 = r3 / a33;
        const double n2 = (r2 - a23 * n3) / a22;
        const double n1 = (r1 - a12 * n2 - a13 * n3) / a11;
        const double n0 = (r0 - a01 * n1 - a02 * n2 - a03 * n3) / a00;
        v[0] = n0; v[1] = n1; v[2] = n2; v[3] = n3;
        fbLp = n3 + fbA * (fbLp - n3);
        return n3;
    }

    double gauss() {
        double a = 0;
        for (int i = 0; i < 4; i++) { uint32_t x = rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rng = x; a += x / 4294967296.0; }
        return (a - 2) * 1.7320508;
    }

    // one sample at the oversampled rate
    double tick() {
        if (slide) cv = cvTarget + slideA * (cv - cvTarget);
        else cv = cvTarget;
        const double I = iVcoRef * std::pow(2.0, cv - c[P::CV_TUNE] + tuneOct);
        const double dv = I / c[P::C33] * h;
        vSaw += dv;
        double corrNow = 0;
        if (vSaw >= c[P::V_HI]) {
            vSaw -= swing;
            const double ph = std::min(std::max((vSaw - c[P::V_LO]) / dv, 0.0), 1.0);
            sawDelay -= 0.5 * swing * ph * ph;
            corrNow = 0.5 * swing * (1 - ph) * (1 - ph);
        }
        const double saw = sawDelay;
        sawDelay = vSaw + corrNow;

        double wave;
        if (square) {
            sqHp = sqA * (sqHp + saw - sqPrevIn);
            sqPrevIn = saw;
            const double sw = c[P::SQ_HI] - c[P::SQ_LO];
            const double lin = 0.5 - sqHp * sqGain / sw;
            const double sat = lin <= 0 ? 0 : lin >= 1 ? 1 : lin * lin * (3 - 2 * lin);
            const double out = c[P::SQ_LO] + sw * sat;
            sqLp = out + sqLpA * (sqLp - out);
            wave = sqLp - (c[P::SQ_LO] + c[P::SQ_HI]) / 2;
        } else {
            sqPrevIn = saw;
            wave = saw - (c[P::V_LO] + c[P::V_HI]) / 2;
        }

        if (envAttack > 0) {
            envVcf = c[P::V_ENV] + envAtkA * (envVcf - c[P::V_ENV]);
            if (envVcf > 0.99 * c[P::V_ENV]) envAttack = 0;
        } else envVcf *= accentNote ? envDecAccA : envDecA;
        if (gate) {
            if (vcaAttack > 0) {
                envVca = 1 + (accentNote ? vcaAtkA : vcaSoftA) * (envVca - 1);
                if (envVca > 0.99) vcaAttack = 0;
            } else envVca *= vcaDecA;
        } else envVca *= vcaRelA;
        const double vAccIn = accentNote ? envVcf * accFrac : 0;
        const double drive = vAccIn - c[P::V_D] - vAcc;
        if (drive > 0) vAcc += drive * accChgK;
        vAcc *= accDisA;
        if (c[P::WOW] > 0 || vWowC > 1e-6) {
            const double vs = vAccIn - c[P::V_D];
            const bool on = vs > wowOff.b * vWowC; const WowTap& t = on ? wowOn : wowOff;
            vWow = t.a * vs + t.b * vWowC;
            const double tgt = on ? t.tgt * vs : 0;
            vWowC = tgt + t.k * (vWowC - tgt);
        }
        const double vAccF = c[P::WOW] > 0 ? vAcc + c[P::WOW] * (vWow - vAcc) : vAcc;

        const double vEnvMod = accentNote ? 0 : envVcf * envModFrac;
        const double vb = wCut * vCutPot + wEnv * vEnvMod + wAcc * vAccF;
        const double Il = std::min(c[P::I_REF] * std::exp(vb / VT), 2e-3);

        double nV = 0, nO = 0, nX = 0;
        if (noiseOn) { nV = gauss() * nVcf; nO = gauss() * nOta; nX = gauss() * nOut; }
        const double y = vcfStep(wave * inAtt + nV, Il);

        const double ya = y * c[P::A_OUT];
        hp14 = hp14A * (hp14 + ya - hp14In); hp14In = ya;
        const double vo = hp14;
        hp49 = hp49A * (hp49 + vo - hp49In); hp49In = vo;
        const double vr = vo * knobs[K::resonance];
        hp76 = hp76A * (hp76 + vr - hp76In); hp76In = vr;
        const double vin = (hp49 / c[P::R88] + hp76 / c[P::R100]) / vinDen + nO;
        const double Iabc = std::max(envVca * c[P::V_VCA] - 0.6, 0.0) / c[P::R105] + std::max(vAcc - 0.6, 0.0) / (c[P::R25] + c[P::R26]);
        const double iout = Iabc * std::tanh(vin / (2 * VT));
        const double vout = iout * c[P::R_OTA];
        hp38 = hp38A * (hp38 + vout - hp38In); hp38In = vout;
        double o = hp38 * vol * 4.0 + nX * 4.0;
        if (fxRat) o = rat(o);
        return 0.95 * std::tanh(o / 0.95);
    }

    double decimate(double x) {
        for (auto& s : decim) {
            const double y = s.b0 * x + s.b1 * s.x1 + s.b2 * s.x2 - s.a1 * s.y1 - s.a2 * s.y2;
            s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y; x = y;
        }
        return x;
    }
};

// ---------------------------------------------------------------------------------------------------
// Knob and part changes glide to their new value (one-pole, ~15 ms), as in the web app.
class ParamSlew {
public:
    ParamSlew(Voice& v, double fs, double tau = 0.015) : v(v), fs(fs), tau(tau) {}
    static bool instant(bool isKnob, int i) { return isKnob ? i == K::lexprog : i >= P::LEX_FS; }
    void set(bool isKnob, int i, double value) {
        if (instant(isKnob, i) || !std::isfinite(value)) {
            erase(isKnob, i);
            if (isKnob) v.setKnob(i, value); else v.setComponent(i, value);
            return;
        }
        const double cur = isKnob ? v.knobs[i] : v.c[i];
        for (auto& e : t) if (e.isKnob == isKnob && e.i == i) { e.value = value; e.log = !isKnob && value > 0 && cur > 0; return; }
        t.push_back({ isKnob, i, value, !isKnob && value > 0 && cur > 0 });
    }
    bool active() const { return !t.empty(); }
    void tick(int n) {
        if (t.empty()) return;
        const double a = 1 - std::exp(-n / (tau * fs));
        for (size_t j = 0; j < t.size();) {
            auto& e = t[j];
            double& cur = e.isKnob ? v.knobs[e.i] : v.c[e.i];
            double nx = e.log ? std::exp(std::log(cur) + (std::log(e.value) - std::log(cur)) * a) : cur + (e.value - cur) * a;
            bool done = false;
            if (std::fabs(nx - e.value) <= 1e-4 * std::max(1e-9, std::fabs(e.value)) || (!e.log && std::fabs(nx - e.value) < 1e-5)) { nx = e.value; done = true; }
            cur = nx;
            if (done) t.erase(t.begin() + (long)j); else ++j;
        }
        v.derive();
    }
private:
    struct Entry { bool isKnob; int i; double value; bool log; };
    void erase(bool isKnob, int i) { for (size_t j = 0; j < t.size(); ++j) if (t[j].isKnob == isKnob && t[j].i == i) { t.erase(t.begin() + (long)j); return; } }
    Voice& v; double fs, tau; std::vector<Entry> t;
};

// ---------------------------------------------------------------------------------------------------
// De-click: delays the output a few ms and eases the level to zero and back around each retrigger.
class DeClick {
public:
    DeClick(Voice& v, double fs, bool on_ = true, double fadeMs = 1.5, double lookMs = 3) : v(v), fs(fs), on(on_) {
        D = std::max(1, (int)jsRound(lookMs * 1e-3 * fs));
        bufL.assign((size_t)D, 0.0f);
        setFade(fadeMs);
    }
    int latency() const { return on ? D : 0; }
    bool isOn() const { return on; }
    void setFade(double ms) { F = std::max(1, std::min(D, (int)jsRound(ms * 1e-3 * fs))); }
    void setOn(bool o) { if (o == on) return; on = o; std::fill(bufL.begin(), bufL.end(), 0.0f); ev.clear(); }
    void noteOn(int code, bool accent, bool slide) { if (!(v.gate && slide)) mark(); v.noteOn(code, accent, slide); }
    void noteOff() { if (v.gate && offs) mark(); v.noteOff(); }
    void process(float* out, int start, int end, float* outR) {
        if (!on) { v.process(out, start, end, outR); t += end - start; return; }
        const bool lex = v.fxLex, dly = v.fxDly, fx = lex || dly;
        v.fxLex = v.fxDly = false; v.process(out, start, end, nullptr); v.fxLex = lex; v.fxDly = dly;
        for (int n = start; n < end; n++, t++) {
            double y = bufL[(size_t)w]; bufL[(size_t)w] = out[n]; w = w + 1 == D ? 0 : w + 1;
            const int64_t tt = t - D;
            while (!ev.empty() && ev.front() <= tt - F) ev.erase(ev.begin());
            if (!ev.empty()) {
                double g = 1;
                for (auto e : ev) {
                    const double d = (double)std::llabs(tt - e);
                    if (d < F) { const double s = 0.5 - 0.5 * std::cos(PI * d / F); if (s < g) g = s; }
                }
                y *= g;
            }
            if (fx) v.fxOut(y, out, outR, n);
            else { out[n] = (float)y; if (outR) outR[n] = (float)y; }
        }
    }
private:
    void mark() { if (on) ev.push_back(t); }
    Voice& v; double fs; bool on; bool offs = false; int D, F = 1; std::vector<float> bufL; int w = 0; int64_t t = 0; std::vector<int64_t> ev;
};

// ---------------------------------------------------------------------------------------------------
struct Step { int code = 33; bool gate = true, accent = false, slide = false; };

// The player around the voice: parameter slew, de-click and the step sequencer (TB303Engine in the JS).
// In the plugin, the processor either lets render() walk the pattern on its own clock (free run, as in
// the web app) or triggers notes itself (host sync, MIDI) and calls render() with playing off.
class Engine {
public:
    explicit Engine(double fs, uint32_t noiseSeed = 0x9e3779b9u) : fs(fs), v(fs, noiseSeed), slew(v, fs), dc(v, fs) {}
    double fs;
    Voice v; ParamSlew slew; DeClick dc;
    std::vector<Step> pattern;
    bool playing = false; double bpm = 128, shuffle = 0, gateLen = 0.5;
    int step = -1;

    void setBpm(double b) { bpm = b; v.bpm = b; v.dly.derive(v.knobs, b); }
    void setPlaying(bool p) { playing = p; step = -1; ctr = 0; if (!p) noteOff(); }
    void noteOn(int code, bool accent, bool slide) { dc.noteOn(code, accent, slide); }
    void noteOff() { dc.noteOff(); }

    void render(float* out, float* outR, int s, int e) {
        int i = s;
        while (i < e) {
            if (playing && ctr <= 0) { stepLen = startStep(); if (!stepLen) stepLen = 1; ctr = stepLen; }
            int run = e - i;
            if (slew.active()) { run = std::min(run, 32); slew.tick(run); }
            if (playing) {
                run = std::min(run, ctr);
                const int elapsed = stepLen - ctr;
                if (gateOffAt > elapsed) run = std::min(run, gateOffAt - elapsed);
                else if (gateOffAt == elapsed) { noteOff(); gateOffAt = -1; }
            }
            dc.process(out, i, i + run, outR);
            i += run; if (playing) ctr -= run;
        }
    }

private:
    int ctr = 0, stepLen = 1, gateOffAt = -1;
    int startStep() {
        const int n = (int)pattern.size(); if (!n) return 0;
        step = (step + 1) % n;
        const Step& s = pattern[(size_t)step]; const Step& prev = pattern[(size_t)((step - 1 + n) % n)];
        const int len = (int)jsRound(fs * 60 / bpm / 4 * (1 + ((step & 1) ? -shuffle : shuffle)));
        if (s.gate) {
            noteOn(s.code, s.accent, prev.gate && prev.slide);
            gateOffAt = s.slide ? -1 : std::max(1, (int)jsRound(len * gateLen));
        } else { noteOff(); gateOffAt = -1; }
        return len;
    }
};

} // namespace tb303
