// The plug-in's window: the web app's own page in a web view, scaled to fit like the web app (fit.js), 1000 x 647.
#pragma once
#include "Processor.h"

class TB303Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit TB303Editor (TB303Processor& p) : AudioProcessorEditor (p), proc (p) {
        setResizable (true, true);
        setResizeLimits (500, 324, 2400, 1553);
        if (auto* c = getConstrainer()) c->setFixedAspectRatio (1000.0 / 647.0);
        makeBrowser();
        setSize (1000, 647);
        proc.onStateLoaded = [this] { makeBrowser(); resized(); };
        proc.onSave = [this] (const juce::String& name, const juce::String& text) { saveFile (name, text); };
        startTimerHz (30);
    }
    ~TB303Editor() override { proc.onStateLoaded = nullptr; proc.onSave = nullptr; }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff181412)); }
    void resized() override { if (browser) browser->setBounds (getLocalBounds()); }

private:
    TB303Processor& proc;
    std::unique_ptr<juce::WebBrowserComponent> browser;
    std::unique_ptr<juce::FileChooser> chooser;
    int sentStep = -2; double sentBpm = -1; bool ready = false;

    void makeBrowser() {
        ready = false; sentStep = -2; sentBpm = -1;
        const auto root = juce::WebBrowserComponent::getResourceProviderRoot();
        auto opts = juce::WebBrowserComponent::Options {}
            .withNativeIntegrationEnabled()
            .withKeepPageLoadedWhenBrowserIsHidden()
            .withResourceProvider ([this] (const juce::String& url) { return proc.getResource (url); }, juce::URL (root).getOrigin())
            .withInitialisationData ("tb303", proc.pageInitData())
            .withEventListener ("tb303", [this] (const juce::var& m) {
                if (m["type"].toString() == "ready") { ready = true; sentStep = -2; sentBpm = -1; }
                proc.handleUiMessage (m);
            });
        if (browser) removeChildComponent (browser.get());
        browser = std::make_unique<juce::WebBrowserComponent> (opts);
        addAndMakeVisible (*browser);
        browser->goToURL (root);
    }

    void emit (const juce::var& v) { if (browser && ready) browser->emitEventIfBrowserIsVisible ("tb303", v); }
    static juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> kv) {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        for (const auto& [k, v] : kv) o->setProperty (k, v);
        return juce::var (o.get());
    }

    void timerCallback() override {
        if (!ready) { int s, a, b; while (proc.popMidi (s, a, b)) {} return; }
        const int st = proc.uiStep();
        if (st != sentStep) { sentStep = st; emit (obj ({ { "type", "step" }, { "step", st } })); }
        const double bpm = proc.uiBpm();
        if (bpm != sentBpm) { sentBpm = bpm; emit (obj ({ { "type", "bpm" }, { "value", bpm } })); }
        int s, a, b;
        while (proc.popMidi (s, a, b)) emit (obj ({ { "type", "midi" }, { "data", juce::Array<juce::var> { s, a, b } } }));
    }

    void saveFile (const juce::String& name, const juce::String& text) {
        chooser = std::make_unique<juce::FileChooser> ("Save", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name), "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [text] (const juce::FileChooser& fc) { const auto f = fc.getResult(); if (f != juce::File {}) f.replaceWithText (text); });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TB303Editor)
};
