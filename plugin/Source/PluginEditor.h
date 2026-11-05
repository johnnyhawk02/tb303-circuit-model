#pragma once
#include "PluginProcessor.h"

// The plug-in's window is the web app's page (Resources/ui.html, made by build.js) in a web view, served from the
// plug-in's binary data. Messages go both ways as 'tb303' events (plugin/web/shim.js, src/js/app/plugin.js).
class TB303Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit TB303Editor(TB303Processor&);
    ~TB303Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    // the web app's screen is a 1000 x 647 pane; the page scales it to the window with a 16 px margin (fit.js)
    static constexpr int W = 1016, H = 663;

private:
    std::optional<juce::WebBrowserComponent::Resource> resource(const juce::String& url);
    juce::WebBrowserComponent::Options options();
    void load();
    void onMessage(const juce::var& m);
    void emit(const juce::var& m) { web.emitEventIfBrowserIsVisible("tb303", m); }
    void timerCallback() override;

    TB303Processor& proc;
    juce::WebBrowserComponent web;
    bool ready = false;
    int lastStep = -2, stateSeen = 0;
    double lastBpm = -1;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TB303Editor)
};
