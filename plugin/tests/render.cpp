// Renders tests/scenario.json with the C++ port and writes raw float32 stereo to argv[2].
// Tiny hand-rolled reader for the scenario: it only needs numbers, strings and the event list.
#include "../Source/dsp/TB303Model.h"
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <variant>
#ifdef __SSE__
#include <xmmintrin.h>
#endif

struct J;
using JObj = std::map<std::string, J>;
using JArr = std::vector<J>;
struct J { std::variant<std::nullptr_t, double, std::string, bool, JArr, JObj> v; };
struct Parser {
    const std::string& s; size_t i = 0;
    void ws() { while (i < s.size() && isspace((unsigned char)s[i])) i++; }
    J parse() {
        ws(); char c = s[i];
        if (c == '{') { i++; JObj o; ws(); if (s[i] == '}') { i++; return { o }; }
            for (;;) { ws(); std::string k = str(); ws(); i++; o[k] = parse(); ws(); if (s[i++] == '}') break; } return { o }; }
        if (c == '[') { i++; JArr a; ws(); if (s[i] == ']') { i++; return { a }; }
            for (;;) { a.push_back(parse()); ws(); if (s[i++] == ']') break; } return { a }; }
        if (c == '"') return { str() };
        if (s.compare(i, 4, "true") == 0) { i += 4; return { true }; }
        if (s.compare(i, 5, "false") == 0) { i += 5; return { false }; }
        size_t n; double d = std::stod(s.substr(i), &n); i += n; return { d };
    }
    std::string str() { i++; std::string r; while (s[i] != '"') r += s[i++]; i++; return r; }
};
static double num(const J& j) { return std::get<double>(j.v); }
static const JObj& obj(const J& j) { return std::get<JObj>(j.v); }
static const JArr& arr(const J& j) { return std::get<JArr>(j.v); }

int main(int argc, char** argv) {
#ifdef __SSE__
    _mm_setcsr(_mm_getcsr() | 0x8040); // FTZ + DAZ, as the plugin runs
#endif
    std::ifstream f(std::string(argc > 2 ? argv[2] : SCENARIO)); /* argv[2]: another scenario file */ std::stringstream ss; ss << f.rdbuf(); std::string text = ss.str();
    Parser p { text }; J root = p.parse(); const auto& S = obj(root);
    const double fs = num(S.at("fs")); const int block = (int)num(S.at("block"));
    tb303::Engine e(fs, (uint32_t)(int64_t)num(S.at("seed")));
    e.setBpm(num(S.at("bpm")));
    e.shuffle = num(obj(S.at("groove")).at("shuffle")); e.gateLen = num(obj(S.at("groove")).at("gate"));
    for (const auto& st : arr(S.at("pattern"))) { const auto& a = arr(st); e.pattern.push_back({ (int)num(a[0]), num(a[3]) != 0, num(a[1]) != 0, num(a[2]) != 0 }); }
    const int N = (int)tb303::jsRound(fs * num(S.at("seconds")));
    std::vector<float> L((size_t)N), R((size_t)N);
    const auto& ev = arr(S.at("events")); size_t ei = 0; bool started = false;
    for (int i = 0; i < N; i += block) {
        while (ei < ev.size() && num(obj(ev[ei]).at("at")) * fs <= i) {
            const auto& x = obj(ev[ei++]);
            if (x.count("knobs")) for (auto& [k, val] : obj(x.at("knobs"))) e.slew.set(true, tb303::knobIndex(k), num(val));
            if (x.count("parts")) for (auto& [k, val] : obj(x.at("parts"))) e.slew.set(false, tb303::partIndex(k), num(val));
            if (x.count("fx")) for (auto& [k, val] : obj(x.at("fx"))) { bool on = std::get<bool>(val.v); if (k == "rat") e.v.fxRat = on; if (k == "lex") e.v.fxLex = on; if (k == "dly") e.v.fxDly = on; if (k == "fold") e.v.fxFold = on; if (k == "flg") e.v.fxFlg = on; }
            if (x.count("tune")) { double t[12]; for (int k = 0; k < 12; k++) t[k] = num(arr(x.at("tune"))[k]); e.v.setTune(t); }
            if (x.count("wave")) e.v.square = std::get<std::string>(x.at("wave").v) == "square";
        }
        if (!started) { e.setPlaying(true); started = true; }
        const int n = std::min(block, N - i);
        e.render(L.data() + i, R.data() + i, 0, n);
    }
    std::vector<float> out((size_t)N * 2);
    for (int i = 0; i < N; i++) { out[2 * (size_t)i] = L[(size_t)i]; out[2 * (size_t)i + 1] = R[(size_t)i]; }
    FILE* o = fopen(argv[1], "wb"); fwrite(out.data(), 4, out.size(), o); fclose(o);
}
