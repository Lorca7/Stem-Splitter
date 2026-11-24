#pragma once

#include "SpectrumView.h"
#include "PluginProcessor.h"

namespace stemsplitter
{

/** Right-hand "Clean up" tab: background noise, mains hum, tone notches and rumble. */
class CleanupPanel final : public juce::Component
{
public:
    explicit CleanupPanel (StemSplitterProcessor& p);
    ~CleanupPanel() override;

    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Content;

    StemSplitterProcessor& processor;
    std::unique_ptr<Content> content;
    juce::Viewport viewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CleanupPanel)
};

} // namespace stemsplitter
