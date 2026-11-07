#pragma once

#include "Theme.h"
#include "PluginProcessor.h"

namespace stemsplitter
{

/** Modal card explaining that an engine needs its neural-network model, with one-click download,
    "locate file" for offline installs, and a fallback to the Lite engine. */
class ModelOverlay final : public juce::Component
{
public:
    explicit ModelOverlay (StemSplitterProcessor& p);

    void show (EngineType engine);
    void hide();
    void refresh();

    /** Separate automatically once the download finishes. */
    bool shouldSeparateWhenReady = false;

    std::function<void()> onLocateFiles, onReady;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> getCardBounds() const;

    StemSplitterProcessor& processor;
    EngineType engine = EngineType::fourStems;
    juce::TextButton downloadButton, locateButton { "I have the file..." }, liteButton { "Use Lite instead" }, closeButton { "Not now" };
    juce::String error;
    double progress = 0.0;
    juce::ProgressBar progressBar { progress };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModelOverlay)
};

} // namespace stemsplitter
