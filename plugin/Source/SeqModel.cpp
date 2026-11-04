#include "SeqModel.h"
#include "BinaryData.h"

namespace tb303 {

static Step rest(int code) { return { code, false, false, false }; }
static std::vector<Step> blank(int len = 16) { return std::vector<Step>((size_t)len, rest(24)); }
static bool anyGate(const std::vector<Step>& p) { for (auto& s : p) if (s.gate) return true; return false; }
static bool same(const std::vector<Step>& a, const std::vector<Step>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].code != b[i].code || a[i].gate != b[i].gate || a[i].accent != b[i].accent || a[i].slide != b[i].slide) return false;
    return true;
}

static const std::vector<Step>& example() {
    static const std::vector<Step> p = [] {
        const int rows[16][4] = { { 21, 1, 0, 1 }, { 21, 0, 0, 1 }, { 33, 0, 1, 1 }, { 31, 0, 0, 1 }, { 21, 1, 0, 1 }, { 21, 0, 0, 0 }, { 24, 0, 1, 1 }, { 26, 0, 0, 1 },
                                  { 21, 0, 0, 1 }, { 33, 1, 0, 1 }, { 28, 0, 1, 1 }, { 24, 0, 0, 1 }, { 21, 1, 1, 1 }, { 19, 0, 0, 1 }, { 21, 0, 0, 0 }, { 36, 1, 0, 1 } };
        std::vector<Step> v;
        for (auto& r : rows) v.push_back({ r[0], r[3] != 0, r[1] != 0, r[2] != 0 });
        return v;
    }();
    return p;
}

SeqModel::SeqModel() {
    for (auto& s : slots) s.pattern = blank();
    slots[0].pattern = example();
    refill(false);
}

int SeqModel::indexOf(const juce::String& k) {
    if (k.length() < 2) return -1;
    const int g = k[0] - 'A', n = k.substring(1).getIntValue() - 1;
    return g >= 0 && g < 4 && n >= 0 && n < 12 && k.substring(1).containsOnly("0123456789") ? g * 12 + n : -1;
}

std::vector<Step> SeqModel::sequence(std::vector<int>& offs) const {
    offs.clear();
    if (chain.size() < 2) return cur().pattern;
    std::vector<Step> all;
    for (auto& k : chain) {
        offs.push_back((int)all.size());
        const int i = indexOf(k);
        const auto& p = i >= 0 ? slots[(size_t)i].pattern : blank();
        all.insert(all.end(), p.begin(), p.end());
    }
    offs.push_back((int)all.size());
    return all;
}

std::pair<int, int> SeqModel::chainPos(int i, const std::vector<int>& offs) const {
    if (i < 0 || chain.size() < 2 || offs.size() < 2) return { -1, i };
    int c = 0; while (c < chain.size() - 1 && i >= offs[(size_t)c + 1]) c++;
    return { c, i - offs[(size_t)c] };
}

void SeqModel::select(int g, int n) { selG = juce::jlimit(0, 3, g); selN = juce::jlimit(0, 11, n); tail.clear(); }
void SeqModel::edited() { cur().autoFilled = false; }

void SeqModel::toggleChain(int g, int n) {
    const auto k = key(g, n), c = selKey();
    const int pos = chain.indexOf(k);
    if (pos >= 0) chain.remove(pos);
    else { if (chain.isEmpty() && k != c) chain.add(c); chain.add(k); }
    if (chain.size() == 1 && chain[0] == c && pos >= 0) chain.clear();
}

void SeqModel::pushUndo() {
    undoStack.push_back({ selKey(), cur().pattern });
    if (undoStack.size() > 30) undoStack.erase(undoStack.begin());
}
juce::String SeqModel::undo() {
    if (undoStack.empty()) return {};
    auto u = undoStack.back(); undoStack.pop_back();
    const int i = indexOf(u.k);
    if (u.k != selKey() && i >= 0) select(i / 12, i % 12);
    cur().pattern = u.pattern; tail.clear(); edited();
    return "Undone on " + u.k + ".";
}

std::unique_ptr<Slot>& SeqModel::clip() { static std::unique_ptr<Slot> c; return c; }
void SeqModel::copy() { clip() = std::make_unique<Slot>(cur()); }
bool SeqModel::paste() {
    if (!clip() || clip()->pattern.empty()) return false;
    cur().pattern = clip()->pattern; cur().shuffle = clip()->shuffle; cur().gate = clip()->gate; tail.clear(); edited();
    return true;
}
void SeqModel::clear() { cur().pattern = blank((int)cur().pattern.size()); tail.clear(); edited(); }

void SeqModel::extendTo(int len) {
    auto& P = cur().pattern;
    while ((int)P.size() < std::min(64, len)) {
        if (!tail.empty()) { P.push_back(tail.front()); tail.erase(tail.begin()); }
        else P.push_back(rest(P.empty() ? 24 : P.back().code));
    }
}
void SeqModel::setLength(int n) {
    auto& P = cur().pattern;
    n = juce::jlimit(1, 64, n);
    while ((int)P.size() > n) { tail.insert(tail.begin(), P.back()); P.pop_back(); }
    extendTo(n);
    edited();
}

juce::String SeqModel::duplicate() {
    auto& P = cur().pattern; const int n = (int)P.size();
    int to = 0; for (int x : { 16, 32, 64 }) if (x > n) { to = x; break; }
    if (!to) return "Already at the 64-step maximum.";
    pushUndo();
    std::vector<Step> out; for (int i = 0; i < to; ++i) out.push_back(P[(size_t)(i % n)]);
    P = out; tail.clear(); edited();
    return "Repeated " + juce::String(n) + " step" + (n == 1 ? "" : "s") + " out to " + juce::String(to) + ". Functions > Undo puts it back.";
}

juce::String SeqModel::shuffleNotes() {
    auto& P = cur().pattern; if (P.size() < 2) return {};
    pushUndo();
    const auto before = P;
    for (int tries = 0; tries < 8 && same(P, before); tries++)
        for (int i = (int)P.size() - 1; i > 0; i--) std::swap(P[(size_t)i], P[(size_t)rng.nextInt(i + 1)]);
    tail.clear(); edited();
    return "Shuffled " + juce::String((int)P.size()) + " steps. Tap again for another order; Functions > Undo puts it back.";
}

// ---------- Generate (generate.js) ----------
const std::vector<SeqModel::Source>& SeqModel::sources() {
    static const std::vector<Source> s = [] {
        std::vector<Source> out;
        const auto j = juce::JSON::parse(juce::String::fromUTF8(BinaryData::patterns_json, BinaryData::patterns_jsonSize));
        if (auto* groups = j.getDynamicObject())
            for (auto& g : groups->getProperties())
                if (auto* ps = g.value.getDynamicObject())
                    for (auto& p : ps->getProperties()) {
                        const auto* steps = p.value[1].getArray(); if (!steps) continue;
                        Source src; src.name = g.name.toString().replace("ABL3 ", "") + " / " + p.name.toString();
                        for (auto& v : *steps) { const int x = (int)v; src.pattern.push_back({ x >> 3, (x & 1) != 0, (x & 2) != 0, (x & 4) != 0 }); }
                        if (anyGate(src.pattern)) out.push_back(std::move(src));
                    }
        return out;
    }();
    return s;
}

const std::vector<double>& SeqModel::densities() { static const std::vector<double> d = { -1, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1 }; return d; }
juce::String SeqModel::densityText(int i) { const double d = densities()[(size_t)i]; return d < 0 ? juce::String("Any") : juce::String(juce::roundToInt(d * 100)) + "%"; }

juce::String SeqModel::move(int which, std::vector<Step>& P) {
    const int n = (int)P.size();
    if (which == 0) { // loop: grab a few steps and loop them to the end
        const int k = rint(2, std::max(2, std::min(8, n >> 1))), at = rint(0, n - k), from = at + k <= n - 1 ? rint(at + k, n - 1) : n;
        std::vector<Step> seg(P.begin() + at, P.begin() + at + k);
        for (int i = from; i < n; i++) P[(size_t)i] = seg[(size_t)((i - from) % k)];
        return "looped steps " + juce::String(at + 1) + "-" + juce::String(at + k) + " from step " + juce::String(from + 1) + " to the end";
    }
    if (which == 1) { // reverse
        if (rng.nextBool()) { std::reverse(P.begin(), P.end()); return "reversed"; }
        std::reverse(P.begin() + (n >> 1), P.end()); return "reversed the second half";
    }
    if (which == 2) { // a step plays twice in a row, pushing the rest along
        const int c = rint(1, std::max(1, n >> 3));
        for (int j = 0; j < c; j++) { const int i = rint(0, n - 2); P.insert(P.begin() + i, P[(size_t)i]); P.pop_back(); }
        return "doubled " + juce::String(c) + " step" + (c > 1 ? "s" : "");
    }
    // shuffle blocks
    const int b = n >= 8 && rng.nextBool() ? 4 : 1;
    std::vector<std::vector<Step>> blocks;
    for (int i = 0; i < n; i += b) blocks.emplace_back(P.begin() + i, P.begin() + std::min(n, i + b));
    const int sw = rint(1, std::max(1, (int)blocks.size() >> 2));
    for (int j = 0; j < sw; j++) std::swap(blocks[(size_t)rint(0, (int)blocks.size() - 1)], blocks[(size_t)rint(0, (int)blocks.size() - 1)]);
    P.clear(); for (auto& bl : blocks) P.insert(P.end(), bl.begin(), bl.end());
    return b > 1 ? "swapped " + juce::String(sw) + " beat" + (sw > 1 ? "s" : "") : "swapped " + juce::String(sw) + " step" + (sw > 1 ? "s" : "");
}

GenResult SeqModel::generatePattern(int len, double dens) {
    auto densOf = [](const std::vector<Step>& p) { int on = 0; for (auto& s : p) on += s.gate; return (double)on / (double)p.size(); };
    std::vector<const Source*> pool;
    for (auto& s : sources()) pool.push_back(&s);
    GenResult r;
    if (pool.empty()) { r.pattern = example(); r.src = "Example"; return r; }
    if (dens >= 0) {
        std::stable_sort(pool.begin(), pool.end(), [&](const Source* a, const Source* b) { return std::fabs(densOf(a->pattern) - dens) < std::fabs(densOf(b->pattern) - dens); });
        if (pool.size() > 60) pool.resize(60);
    }
    const Source& src = *pool[(size_t)rng.nextInt((int)pool.size())];
    const auto& sp = src.pattern;
    const int barLen = std::min(len, std::max(16, (int)sp.size()));
    std::vector<Step> P;
    for (int i = 0; i < barLen; ++i) P.push_back(sp[(size_t)(i % (int)sp.size())]);
    int order[4] = { 0, 1, 2, 3 };
    for (int i = 3; i > 0; --i) std::swap(order[i], order[rng.nextInt(i + 1)]);
    const int nm = rint(1, 3);
    for (int i = 0; i < nm; ++i) r.did.add(move(order[i], P));
    std::vector<Step> out = P;
    while ((int)out.size() < len) {
        auto seg = P; move(rint(0, 3), seg);
        for (size_t i = 0; i < seg.size() && (int)out.size() < len; ++i) out.push_back(seg[i]);
    }
    if (len > barLen) r.did.add("bars 2-" + juce::String((len + 15) / 16) + " varied");
    if (dens >= 0) {
        const int want = std::max(1, juce::roundToInt(dens * len));
        std::vector<int> pitches; for (auto& s : out) if (s.gate) pitches.push_back(s.code);
        auto idx = [&](bool g) { std::vector<int> v; for (int i = 0; i < (int)out.size(); ++i) if (out[(size_t)i].gate == g) v.push_back(i);
            for (int i = (int)v.size() - 1; i > 0; --i) std::swap(v[(size_t)i], v[(size_t)rng.nextInt(i + 1)]); return v; };
        int on = 0; for (auto& s : out) on += s.gate;
        auto beat = [](int i) { return i % 4 == 0 ? 0 : i % 2 == 0 ? 1 : 2; };
        if (on < want) { auto v = idx(false); std::stable_sort(v.begin(), v.end(), [&](int a, int b) { return beat(a) < beat(b); });
            for (int i : v) { if (on >= want) break; auto& s = out[(size_t)i]; s.gate = true; if (!pitches.empty()) s.code = pitches[(size_t)rng.nextInt((int)pitches.size())]; s.slide = false; on++; } }
        if (on > want) { auto v = idx(true); std::stable_sort(v.begin(), v.end(), [&](int a, int b) { return beat(a) > beat(b); });
            for (int i : v) { if (on <= want) break; if (i == 0) continue; auto& s = out[(size_t)i]; s.gate = false; s.accent = false; s.slide = false; on--; } }
    }
    if (!anyGate(out)) out[0].gate = true;
    r.pattern = out; r.src = src.name;
    return r;
}

juce::String SeqModel::generate() {
    pushUndo();
    auto g = generatePattern(genBars * 16, densities()[(size_t)genDens]);
    cur().pattern = g.pattern; tail.clear(); edited();
    juce::String d = g.did[0];
    if (g.did.size() > 1) { juce::StringArray a = g.did; a.remove(a.size() - 1); d = a.joinIntoString(", ") + " and " + g.did[g.did.size() - 1]; }
    return "From " + g.src + ": " + d + ". Functions > Undo puts the old one back.";
}

int SeqModel::refill(bool force) {
    int n = 0;
    for (int i = 0; i < 48; ++i) {
        if (i == selG * 12 + selN) continue;
        auto& s = slots[(size_t)i];
        if (!(!anyGate(s.pattern) || (force && s.autoFilled))) continue;
        auto g = generatePattern(genBars * 16, densities()[(size_t)genDens]);
        s.pattern = g.pattern; s.shuffle = 0; s.gate = 0.5; s.autoFilled = true; s.name = g.src; n++;
    }
    return n;
}

juce::String SeqModel::alter() {
    pushUndo();
    auto& P = cur().pattern; const int n = (int)P.size();
    std::vector<int> pitches; for (auto& s : P) if (s.gate && std::find(pitches.begin(), pitches.end(), s.code) == pitches.end()) pitches.push_back(s.code);
    if (pitches.empty()) pitches.push_back(24);
    std::vector<int> idx; for (int i = 0; i < n; ++i) idx.push_back(i);
    for (int i = n - 1; i > 0; --i) std::swap(idx[(size_t)i], idx[(size_t)rng.nextInt(i + 1)]);
    idx.resize((size_t)std::max(1, juce::roundToInt(n / 4.0)));
    int note = 0, octave = 0, accent = 0, slide = 0, rests = 0, gates = 0;
    for (int i : idx) {
        auto& st = P[(size_t)i]; const double r = rng.nextDouble();
        if (r < 0.4 && pitches.size() > 1) { std::vector<int> c; for (int x : pitches) if (x != st.code) c.push_back(x); st.code = c[(size_t)rng.nextInt((int)c.size())]; st.gate = true; note++; }
        else if (r < 0.55) { const bool up = st.code + 12 <= 59 && (st.code - 12 < 0 || rng.nextBool()); st.code += up ? 12 : -12; octave++; }
        else if (r < 0.7) { st.accent = !st.accent; accent++; }
        else if (r < 0.85) { st.slide = !st.slide; slide++; }
        else if (i > 0) { st.gate = !st.gate; st.gate ? gates++ : rests++; }
        else { st.accent = !st.accent; accent++; }
    }
    edited();
    juce::StringArray parts;
    auto add = [&](int v, const char* one, const char* many) { if (v) parts.add(juce::String(v) + " " + (v > 1 ? many : one)); };
    add(note, "note", "notes"); add(octave, "octave jump", "octave jumps"); add(accent, "accent", "accents");
    add(slide, "slide", "slides"); add(rests, "new rest", "new rests"); add(gates, "rest filled", "rests filled");
    return "Altered " + parts.joinIntoString(", ") + ". Functions > Undo puts it back.";
}

juce::String SeqModel::joinChain() {
    juce::StringArray keys = chain.size() >= 2 ? chain : juce::StringArray { selKey() };
    std::vector<Step> all;
    for (auto& k : keys) { const int i = indexOf(k); const auto& p = i >= 0 ? slots[(size_t)i].pattern : blank(); all.insert(all.end(), p.begin(), p.end()); }
    const int total = (int)all.size(); if (all.size() > 64) all.resize(64);
    auto c = std::make_unique<Slot>(); c->pattern = all; c->shuffle = cur().shuffle; c->gate = cur().gate; clip() = std::move(c);
    if (chain.size() < 2) return "No chain, copied " + keys[0] + " (" + juce::String(total) + " steps). Paste it into a slot.";
    return "Joined " + keys.joinIntoString(" > ") + ": " + juce::String(std::min(64, total)) + " steps copied"
        + (total > 64 ? ", the last " + juce::String(total - 64) + " cut off at the 64-step limit" : juce::String()) + ". Paste it into a slot.";
}

// ---------- web app JSON ----------
std::vector<Step> SeqModel::patternFromVar(const juce::var& v) {
    std::vector<Step> p;
    if (auto* a = v.getArray()) {
        if (a->isEmpty() || a->size() > 64) return p;
        for (auto& st : *a) p.push_back({ juce::jlimit(0, 59, (int)st.getProperty("code", 24)), (bool)st.getProperty("gate", false),
                                          (bool)st.getProperty("accent", false), (bool)st.getProperty("slide", false) });
    }
    return p;
}
juce::var SeqModel::patternToVar(const std::vector<Step>& p) {
    juce::Array<juce::var> a;
    for (auto& s : p) {
        auto* o = new juce::DynamicObject();
        o->setProperty("code", s.code); o->setProperty("gate", s.gate); o->setProperty("accent", s.accent); o->setProperty("slide", s.slide);
        a.add(juce::var(o));
    }
    return a;
}

juce::var SeqModel::toVar() const {
    auto* b = new juce::DynamicObject();
    for (int i = 0; i < 48; ++i) {
        const auto& s = slots[(size_t)i];
        auto* o = new juce::DynamicObject();
        o->setProperty("shuffle", s.shuffle); o->setProperty("gate", s.gate); o->setProperty("pattern", patternToVar(s.pattern));
        if (s.autoFilled) { o->setProperty("auto", true); o->setProperty("name", s.name); }
        b->setProperty(key(i / 12, i % 12), juce::var(o));
    }
    return juce::var(b);
}

void SeqModel::fromVar(const juce::var& banks, const juce::var& sel, const juce::var& ch) {
    for (auto& s : slots) s = Slot { blank(), 0, 0.5, false, {} };
    if (auto* b = banks.getDynamicObject())
        for (auto& kv : b->getProperties()) {
            const int i = indexOf(kv.name.toString()); if (i < 0) continue;
            auto p = patternFromVar(kv.value.getProperty("pattern", {})); if (p.empty()) continue;
            auto& s = slots[(size_t)i];
            s.pattern = p;
            const auto g = kv.value.getProperty("gate", 0.5), sh = kv.value.getProperty("shuffle", 0.0);
            s.gate = juce::jlimit(0.1, 0.95, (double)g); s.shuffle = juce::jlimit(0.0, 0.5, (double)sh);
            s.autoFilled = (bool)kv.value.getProperty("auto", false); s.name = kv.value.getProperty("name", "").toString();
        }
    const auto g = sel.getProperty("g", "A").toString();
    const int n = (int)sel.getProperty("n", 0);
    select(g.isNotEmpty() && juce::String("ABCD").containsChar(g[0]) ? g[0] - 'A' : 0, n >= 0 && n < 12 ? n : 0);
    chain.clear();
    if (auto* a = ch.getArray()) for (auto& k : *a) if (indexOf(k.toString()) >= 0) chain.add(k.toString());
}

// ---------- auto-namer (namer.js) ----------
// Two or three real words, or one made-up alien/spice word. Same pools and odds as src/js/app/namer.js.
juce::String autoName(const juce::StringArray& taken) {
    juce::Random& R = juce::Random::getSystemRandom();
    const juce::StringArray ADJ {
        "acid", "burnt", "melted", "sideways", "haunted", "soggy", "inverted", "radioactive", "fermented", "exploded", "backwards", "feral",
        "damp", "cursed", "rusty", "velvet", "liquid", "molten", "frozen", "hollow", "crooked", "electric", "magnetic", "toxic", "lunar", "solar",
        "nocturnal", "sticky", "greasy", "fuzzy", "jagged", "wobbly", "squelchy", "rubber", "chrome", "plastic", "neon", "infrared", "ultraviolet",
        "subsonic", "sunken", "drowned", "feverish", "lazy", "nervous", "angry", "polite", "sleepy", "restless", "forbidden", "secret", "lost",
        "forgotten", "ancient", "broken", "bent", "twisted", "spiral", "crystal", "golden", "silver", "copper", "cobalt", "crimson", "violet",
        "amber", "emerald", "obsidian", "ivory", "scarlet", "ochre", "indigo", "saffron", "turquoise", "bitter", "sour", "salty", "spicy", "smoky",
        "pickled", "candied", "toasted", "roasted", "sugared", "slippery", "dusty", "muddy", "misty", "stormy", "humid", "arctic", "tropical",
        "volcanic", "tidal", "orbital", "quantum", "cosmic", "astral", "mutant", "synthetic", "analog", "digital", "resonant", "detuned",
        "overdriven", "squashed", "stretched", "reversed", "mirrored", "phantom", "ghostly", "savage", "gentle", "humble", "royal", "imperial",
        "rogue", "stolen", "borrowed", "blessed", "unholy", "sacred", "pagan", "midnight", "dawn", "dusk", "eternal", "hungry", "thirsty", "dizzy",
        "giddy", "grumpy", "brave", "clumsy", "tiny", "giant", "infinite", "hyper", "mega", "micro", "proto", "turbo", "deep", "shallow", "wet",
        "dry", "hot", "cold", "loose", "tight", "bouncy", "elastic", "warped", "glitched", "static", "dynamic", "hypnotic", "psychic", "strange" };
    const juce::StringArray NOUN {
        "squelch", "resonance", "filter", "cutoff", "envelope", "accent", "slide", "glide", "diode", "ladder", "voltage", "circuit", "transistor",
        "capacitor", "oscillator", "sawtooth", "pulse", "sequencer", "pattern", "step", "tempo", "groove", "bassline", "feedback", "overdrive",
        "fuzz", "reverb", "echo", "delay", "phaser", "flanger", "sidechain", "warehouse", "rave", "strobe", "smiley", "acid", "loop", "signal",
        "static", "thunder", "lightning", "monsoon", "hailstorm", "drizzle", "fog", "tornado", "avalanche", "glacier", "canyon", "swamp", "marsh",
        "bog", "lagoon", "reef", "tide", "riptide", "whirlpool", "geyser", "volcano", "lava", "magma", "basalt", "granite", "quartz", "obsidian",
        "pumice", "sulphur", "mercury", "cobalt", "bismuth", "graphite", "ember", "cinder", "ash", "moss", "lichen", "fungus", "spore", "mycelium",
        "thorn", "bramble", "cactus", "orchid", "lotus", "mangrove", "tundra", "dune", "oasis", "mirage", "eclipse", "aurora", "mollusc",
        "octopus", "squid", "jellyfish", "eel", "lobster", "barnacle", "axolotl", "newt", "salamander", "gecko", "iguana", "chameleon", "toad",
        "beetle", "mantis", "hornet", "moth", "cicada", "scorpion", "tarantula", "leech", "slug", "snail", "worm", "badger", "otter", "weasel",
        "ferret", "wombat", "platypus", "armadillo", "sloth", "lemur", "baboon", "hyena", "jackal", "vulture", "raven", "heron", "pelican",
        "flamingo", "penguin", "walrus", "narwhal", "kraken", "leviathan", "basilisk", "griffin", "chimera", "wyvern", "goblin", "gremlin", "imp",
        "lantern", "saxophone", "trombone", "accordion", "kazoo", "theremin", "bagpipe", "harmonica", "teapot", "kettle", "spoon", "ladle",
        "colander", "umbrella", "bicycle", "tractor", "submarine", "zeppelin", "rocket", "satellite", "robot", "android", "cyborg", "reactor",
        "turbine", "furnace", "boiler", "engine", "piston", "gearbox", "antenna", "radar", "sonar", "laser", "hologram", "mirror", "prism", "lens",
        "compass", "labyrinth", "temple", "pyramid", "ziggurat", "monolith", "obelisk", "cathedral", "bunker", "lighthouse", "harbour", "carnival",
        "circus", "casino", "disco", "basement", "attic", "cellar", "garage", "motorway", "tunnel", "bridge", "factory", "refinery", "laboratory",
        "asylum", "whisper", "murmur", "rumble", "gurgle", "wobble", "bubble", "sludge", "goo", "slime", "jelly", "custard", "gravy", "syrup",
        "treacle", "marmalade", "pickle", "noodle", "dumpling", "waffle", "pancake", "crumpet", "biscuit", "pretzel", "doughnut", "fudge",
        "toffee", "nougat", "tiramisu", "pavlova", "eclair", "baklava", "profiterole", "trifle", "cheesecake", "sorbet", "macaron", "strudel",
        "meringue", "parfait", "clafoutis", "cannoli", "gelato", "souffle", "banoffee", "brulee", "cobbler", "crumble", "flapjack", "brownie",
        "sundae", "mousse", "redshift", "blueshift", "horizon", "quasar", "pulsar", "magnetar", "baryon", "neutrino", "singularity", "nebula",
        "perihelion", "aphelion", "parsec", "supernova", "wormhole", "cepheid", "comet", "asteroid", "meteor", "galaxy", "cosmos", "void", "orbit",
        "equinox", "solstice", "zenith", "nadir", "photon", "graviton", "muon", "gluon", "quark", "boson", "plasma", "fractal", "vortex",
        "tesseract", "paradox", "entropy", "aether", "logos", "kairos", "chronos", "pneuma", "eidolon", "hubris", "nous", "telos", "apeiron",
        "kosmos", "eros", "thanatos", "hypnos", "agora", "oracle", "amphora", "odyssey", "hydra", "gorgon", "elysion", "tartaros", "delphi",
        "ithaka", "cyclops", "minotaur", "siren", "titan", "nemesis", "medusa", "icarus", "prometheus", "pandora", "charon", "styx", "olympus",
        "acropolis" };
    const juce::StringArray SPICE {
        "cinnamon", "cardamom", "turmeric", "saffron", "nutmeg", "clove", "cumin", "coriander", "fenugreek", "paprika", "cayenne", "chilli",
        "pepper", "ginger", "galangal", "sumac", "zaatar", "harissa", "asafoetida", "tamarind", "anise", "fennel", "caraway", "mace", "allspice",
        "juniper", "vanilla", "wasabi", "mustard", "nigella", "ajwain", "mahleb", "grains of paradise", "lemongrass", "kaffir", "sansho",
        "sichuan", "garam masala", "ras el hanout", "berbere", "baharat", "dukkah", "chipotle", "ancho", "habanero", "jalapeno", "oregano",
        "thyme", "rosemary", "tarragon", "sage", "marjoram", "dill", "lovage", "liquorice", "myrrh", "frankincense", "mastic", "pandan", "curry" };
    const juce::StringArray A_PRE {
        "zor", "qua", "vex", "thra", "xil", "oom", "kry", "bla", "nyx", "gor", "fen", "plu", "yth", "ska", "zub", "mor", "quee", "drax", "chth",
        "xan", "vor", "glo", "zha", "kth", "ul", "yog", "shub", "nar", "az", "ix", "thul", "krel", "vrr", "oog", "zee", "phla", "gri", "ssk",
        "tzo", "ka", "xo", "ny", "quo", "zim", "bru", "dzo", "wub", "ruu", "tlo", "mnok", "eek", "ozz", "fth", "gla", "pt", "hra", "jhe", "lyx" };
    const juce::StringArray A_MID {
        "la", "ri", "on", "ex", "ub", "ae", "ix", "ol", "ee", "aa", "u", "yo", "oz", "ath", "ul", "ik", "or", "ei", "uu", "yr", "ag", "esh" };
    const juce::StringArray A_END {
        "th", "x", "nok", "ar", "ium", "ozz", "eep", "ul", "orb", "ak", "ish", "oid", "onoz", "ix", "ux", "yth", "aal", "orr", "ek", "oth", "ulon",
        "ax", "iq", "zz", "oon", "ra", "ka", "ii", "ogg", "esk", "alq", "umph", "izz", "ond", "ath", "yx" };
    auto pickS = [&](const juce::StringArray& a) { return a[R.nextInt(a.size())]; };
    auto pickA = [&](std::initializer_list<const char*> l) { auto it = l.begin(); std::advance(it, R.nextInt((int)l.size())); return juce::String::fromUTF8(*it); };
    auto cap = [](const juce::String& w) { return w.substring(0, 1).toUpperCase() + w.substring(1); };
    auto chance = [&](double p) { return R.nextDouble() < p; };
    auto vowel = [](juce::juce_wchar c) { return juce::String("aeiouy").containsChar(c); };
    juce::StringArray SPICE1; for (auto& s : SPICE) if (!s.containsChar(' ')) SPICE1.add(s);
    auto splice = [&](const juce::String& a, const juce::String& b) {
        juce::Array<int> cuts, tails;
        for (int i = 1; i < a.length() - 1; ++i) if (vowel(a[i])) cuts.add(i + 1);
        for (int i = 1; i < b.length(); ++i) if (!vowel(b[i]) && i >= b.length() / 3.0) tails.add(i);
        return a.substring(0, cuts.isEmpty() ? 2 : cuts[R.nextInt(cuts.size())]) + b.substring(tails.isEmpty() ? b.length() / 2 : tails[R.nextInt(tails.size())]);
    };
    auto syll = [&]() { return pickS(A_PRE) + (chance(0.6) ? pickS(A_MID) : juce::String()) + pickS(A_END); };
    auto spice = [&]() { return pickS(SPICE).removeCharacters(" "); };
    auto alienSpice = [&]() {
        const double r = R.nextDouble();
        juce::String w;
        if (r < 0.3) w = splice(spice(), syll());
        else if (r < 0.55) w = splice(syll(), spice());
        else if (r < 0.75) w = pickS(A_PRE) + spice();
        else if (r < 0.9) w = spice().substring(0, 3 + R.nextInt(3)) + pickS(A_MID) + pickS(A_END);
        else w = syll();
        w = cap(w);
        if (chance(0.3)) w += "-" + cap(chance(0.5) ? syll() : splice(syll(), spice()));
        return w;
    };
    auto greekify = [&](juce::String w) {
        while (w.isNotEmpty() && vowel(w.getLastCharacter())) w = w.dropLastCharacters(1);
        return (chance(0.3) ? pickA({ "proto", "hyper", "neo", "pan", "archi", "meta" }) : juce::String()) + w + pickA({ "os", "ia", "eion", "ikon", "ides", "ax" });
    };
    auto noun = [&]() { return chance(0.12) ? pickS(SPICE1) : chance(0.06) ? greekify(pickS(NOUN)) : pickS(NOUN); };
    auto firstHalf = [](const juce::String& s) { return s.upToFirstOccurrenceOf("-", false, false); };
    const juce::StringArray LINKS { "of", "in", "on", "for", "and", "from", "versus" };
    auto phrase = [&]() -> juce::StringArray {
        const double r = R.nextDouble();
        if (r < 0.25) return { pickS(ADJ), noun() };
        if (r < 0.42) return { noun(), noun() };
        if (r < 0.54) return { pickS(ADJ), noun(), noun() };
        if (r < 0.64) return { pickS(ADJ), pickS(ADJ), noun() };
        if (r < 0.74) return { noun(), pickS(LINKS), noun() };
        if (r < 0.82) return { pickS(SPICE1), noun() };
        if (r < 0.9) return { firstHalf(alienSpice()), noun() };
        if (r < 0.95) return { pickS(ADJ), firstHalf(alienSpice()) };
        return { "the", pickS(ADJ), noun() };
    };
    for (int i = 0; i < 30; i++) {
        juce::String n;
        if (chance(0.3)) n = alienSpice();
        else {
            auto w = phrase();
            juce::StringArray u = w; u.removeDuplicates(false);
            if (u.size() < w.size()) continue;
            for (int j = 0; j < w.size(); ++j) if (!(j && LINKS.contains(w[j]))) w.set(j, cap(w[j]));
            n = w.joinIntoString(" ");
        }
        juce::String z;
        for (auto c : n) z += juce::String::charToString(c == 's' && chance(0.6) ? (juce::juce_wchar)'z' : c == 'S' && chance(0.6) ? (juce::juce_wchar)'Z' : c);
        if (z.length() <= 32 && !taken.contains(z)) return z;
    }
    return "Preset " + juce::String(taken.size() + 1);
}

} // namespace tb303
