#pragma once

#include "Theme.h"
#include "Core/FrequencyCut.h"

namespace stemsplitter
{

/** Response-curve editor for the frequency cut: drag the handles to set the edges, drag inside a band
    to move it, scroll to widen/narrow it. */
class FrequencyCutView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    FrequencyCutView();

    void setSettings (const FrequencyCutSettings& s);
    const FrequencyCutSettings& getSettings() const noexcept { return settings; }

    std::function<void (const FrequencyCutSettings&)> onChange;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Handle { none, low, high, band };

    juce::Rectangle<float> getPlotArea() const;
    float xForFrequency (float hz) const;
    float frequencyForX (float x) const;
    float yForDb (float db) const;
    Handle handleAt (juce::Point<float>) const;
    void update (FrequencyCutSettings s);

    FrequencyCutSettings settings;
    Handle hovered = Handle::none, dragging = Handle::none;
    float dragStartLow = 0.0f, dragStartHigh = 0.0f;

    static constexpr float minDb = -60.0f;
};

} // namespace stemsplitter
