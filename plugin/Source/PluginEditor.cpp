#include "PluginEditor.h"
#include "BinaryData.h"

static juce::var obj(std::initializer_list<std::pair<const char*, juce::var>> kv) {
    auto* o = new juce::DynamicObject();
    for (auto& [k, v] : kv) o->setProperty(k, v);
    return juce::var(o);
}

juce::WebBrowserComponent::Options TB303Editor::options() {
    auto o = juce::WebBrowserComponent::Options {}
                 .withNativeIntegrationEnabled()
                 .withKeepPageLoadedWhenBrowserIsHidden()
                 .withEventListener("tb303", [this](const juce::var& m) { onMessage(m); });
#if ! JUCE_LINUX
    o = o.withResourceProvider([this](const juce::String& url) { return resource(url); });
#endif
    return o;
}

TB303Editor::TB303Editor(TB303Processor& p)
    : AudioProcessorEditor(p), proc(p),
      web(options()) {
    stateSeen = proc.stateVersion.load();
    addAndMakeVisible(web);
    setResizable(true, true);
    setResizeLimits(W * 6 / 10, H * 6 / 10, W * 2, H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double)W / H);
    setSize(W, H);
    load();
    startTimerHz(30);
}

TB303Editor::~TB303Editor() { stopTimer(); SharedStore::get().flush(); }

void TB303Editor::load() {
#if JUCE_LINUX
    // Linux (development only): JUCE's WebKitGTK helper process can't take an 800 kB resource over its pipe, so
    // the page and its files go to a temporary folder instead
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("tb303-ui");
    dir.getChildFile("irs").createDirectory();
    for (auto* f : { "/index.html", "/VT323-Regular.ttf", "/irs/irs.json" })
        if (auto r = resource(f)) dir.getChildFile(juce::String(f).substring(1)).replaceWithData(r->data.data(), r->data.size());
    web.goToURL(juce::URL(dir.getChildFile("index.html")).toString(false));
#else
    web.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
#endif
}

void TB303Editor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff181412)); }
void TB303Editor::resized() { web.setBounds(getLocalBounds()); }

std::optional<juce::WebBrowserComponent::Resource> TB303Editor::resource(const juce::String& url) {
    const auto path = url.upToFirstOccurrenceOf("?", false, false);
    auto bytes = [](const char* d, int n) { std::vector<std::byte> v((size_t)n); std::memcpy(v.data(), d, (size_t)n); return v; };
    if (path == "/" || path == "/index.html") {
        const juce::String html = juce::String::fromUTF8(BinaryData::ui_html, BinaryData::ui_htmlSize).replace("<!--@boot-->", proc.bootScript());
        const auto utf8 = html.toUTF8();
        return juce::WebBrowserComponent::Resource { bytes(utf8.getAddress(), (int)utf8.sizeInBytes() - 1), "text/html" };
    }
    if (path == "/VT323-Regular.ttf") return juce::WebBrowserComponent::Resource { bytes(BinaryData::VT323Regular_ttf, BinaryData::VT323Regular_ttfSize), "font/ttf" };
    if (path == "/irs/irs.json") return juce::WebBrowserComponent::Resource { bytes(BinaryData::irs_json, BinaryData::irs_jsonSize), "application/json" };
    return std::nullopt;
}

void TB303Editor::onMessage(const juce::var& m) {
    const auto type = m.getProperty("type", {}).toString();
    if (type == "ready") {
        ready = true; lastStep = -2; lastBpm = -1;
        emit(obj({ { "type", "rate" }, { "value", proc.sampleRateNow() } }));
        return;
    }
    if (type == "save") { // Export all / Export favourites: the web app downloads a file; here a save panel asks where
        const auto name = juce::File::createLegalFileName(m.getProperty("name", "tb303-presets.json").toString());
        const auto text = m.getProperty("text", {}).toString();
        chooser = std::make_unique<juce::FileChooser>("Export", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(name), "*.json");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [text](const juce::FileChooser& fc) { const auto f = fc.getResult(); if (f != juce::File()) f.replaceWithText(text); });
        return;
    }
    proc.handleMessage(m);
}

void TB303Editor::timerCallback() {
    proc.endGestures();
    if (const int sv = proc.stateVersion.load(); sv != stateSeen) { // the host loaded another state: start the page again from it
        stateSeen = sv; ready = false;
        load();
        return;
    }
    if (!ready) return;
    if (const int s = proc.currentStep.load(); s != lastStep) { lastStep = s; emit(obj({ { "type", "step" }, { "step", s } })); }
    if (const double b = proc.hostBpm.load(); b != lastBpm) { lastBpm = b; emit(obj({ { "type", "bpm" }, { "value", b } })); }
    juce::Array<juce::var> changes;
    proc.collectChanges(changes);
    for (auto& c : changes) emit(c);
}
