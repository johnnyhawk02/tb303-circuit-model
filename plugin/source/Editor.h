// The plug-in's window: the web app's own page in a web view, scaled to fit like the web app (fit.js), 1000 x 647.
#pragma once
#include "Processor.h"
#include "KeyMonitor.h"

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
    ~TB303Editor() override { tbRemoveKeyMonitor (keyMonitor); proc.endGestures(); proc.onStateLoaded = nullptr; proc.onSave = nullptr; }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff181412)); }
    void resized() override { if (browser) browser->setBounds (getLocalBounds()); }

private:
    TB303Processor& proc;
    std::unique_ptr<juce::WebBrowserComponent> browser;
    std::unique_ptr<juce::FileChooser> chooser;
    int sentStep = -2; double sentBpm = -1; bool ready = false, typing = false;
    void* keyMonitor = nullptr; bool keyMonitorTried = false;

    void makeBrowser() {
        ready = false; sentStep = -2; sentBpm = -1;
        const auto root = juce::WebBrowserComponent::getResourceProviderRoot();
        auto opts = juce::WebBrowserComponent::Options {}
           #if JUCE_WINDOWS
            // Edge WebView2 (the old IE control can't run the page). Its cache goes in the user's app data folder:
            // the default, beside the DAW's .exe, is often not writable
            .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
            .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}
                .withUserDataFolder (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("303 Circuit Model").getChildFile ("WebView2"))
                .withBackgroundColour (juce::Colour (0xff0b0c0a)))
           #endif
            .withNativeIntegrationEnabled()
            .withKeepPageLoadedWhenBrowserIsHidden()
            .withResourceProvider ([this] (const juce::String& url) { return proc.getResource (url); }, juce::URL (root).getOrigin())
            .withInitialisationData ("tb303", proc.pageInitData())
            .withEventListener ("tb303", [this] (const juce::var& m) {
                if (m["type"].toString() == "typing") { typing = (bool) m["on"]; return; }
                if (m["type"].toString() == "ready") { ready = true; sentStep = -2; sentBpm = -1; proc.uiOpened(); }
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

    // the page's own Cmd+C / Cmd+V / Cmd+Z (copy, paste, undo the pattern); in a text field they stay the field's
    void watchCmdKeys() {
        keyMonitorTried = true;
        auto* peer = getPeer(); if (peer == nullptr) { keyMonitorTried = false; return; }
        keyMonitor = tbAddKeyMonitor (peer->getNativeHandle(), [this] (const char* key, bool shift) {
            const juce::String k (key);
            if (typing || ! ready || ! (k == "c" || k == "v" || k == "z")) return false;
            emit (obj ({ { "type", "key" }, { "key", k }, { "shift", shift } }));
            return true;
        });
    }

    void timerCallback() override {
        if (! keyMonitorTried) watchCmdKeys();
        if (!ready) { int s, a, b; while (proc.popMidi (s, a, b)) {} return; }
        const int st = proc.uiStep();
        if (st != sentStep) { sentStep = st; emit (obj ({ { "type", "step" }, { "step", st } })); }
        const double bpm = proc.uiBpm();
        if (bpm != sentBpm) { sentBpm = bpm; emit (obj ({ { "type", "bpm" }, { "value", bpm } })); }
        int s, a, b;
        while (proc.popMidi (s, a, b)) emit (obj ({ { "type", "midi" }, { "data", juce::Array<juce::var> { s, a, b } } }));
        // DAW automation moves the controls on the screen
        proc.uiTick ([this] (const juce::var& m) {
            juce::DynamicObject::Ptr o = new juce::DynamicObject(); o->setProperty ("type", "param"); o->setProperty ("m", m);
            emit (juce::var (o.get()));
        });
    }

    void saveFile (const juce::String& name, const juce::String& text) {
        chooser = std::make_unique<juce::FileChooser> ("Save", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name), "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [text] (const juce::FileChooser& fc) { const auto f = fc.getResult(); if (f != juce::File {}) f.replaceWithText (text); });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TB303Editor)
};
