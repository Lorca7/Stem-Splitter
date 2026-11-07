#pragma once

#include "Theme.h"
#include "PluginProcessor.h"

namespace stemsplitter
{

/** Bottom bar: play/pause, back to start, loop, time display and hints. */
class TransportBar final : public juce::Component
{
public:
    explicit TransportBar (StemSplitterProcessor& p);
    ~TransportBar() override;

    /** Called from the editor's timer. */
    void update();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    StemSplitterProcessor& processor;
    std::unique_ptr<IconButton> playButton, startButton, loopButton;
    juce::String timeText;
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace stemsplitter
