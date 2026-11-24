#pragma once

#include "Theme.h"
#include "Core/NoiseCleanup.h"

namespace stemsplitter
{

/** Shows the recording's spectrum (average and steady noise floor) with the clean-up filters on top.
    Click a peak to notch it out, drag a notch to move it, double-click (or right-click) one to remove it. */
class SpectrumView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SpectrumView();

    void setData (std::shared_ptr<const CleanupAnalysis> analysis, const CleanupSettings& settings, float humHz);

    std::function<void (std::vector<float> notches)> onNotchesChanged;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plotArea() const;
    float xFor (float hz) const;
    float hzFor (float x) const;
    float yFor (float db) const;
    int notchAt (float x) const;
    void commit();

    std::shared_ptr<const CleanupAnalysis> analysis;
    CleanupSettings settings;
    float humHz = 0.0f;
    std::vector<float> notches;
    int hoveredNotch = -1, draggingNotch = -1;
    float hoverHz = 0.0f;
    bool hovering = false;

    static constexpr float topDb = 0.0f, bottomDb = -110.0f;
};

} // namespace stemsplitter
