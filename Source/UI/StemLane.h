#pragma once

#include "Theme.h"
#include "Core/Session.h"

#include <optional>

namespace stemsplitter
{

/** One horizontal track: name, mute/solo, gain, waveform with playhead, export checkbox and a handle
    you can drag straight into Finder or a DAW. The "original" lane (no stem type) shows the source. */
class StemLane final : public juce::Component
{
public:
    explicit StemLane (std::optional<StemType> type);
    ~StemLane() override;

    std::optional<StemType> getType() const noexcept { return type; }

    void setWaveform (const WaveformPeaks* peaks);
    void setMixState (const StemMixState& state, bool audible);
    void setExportSelected (bool selected);
    void setShowExportToggle (bool show);
    void setSubtitle (const juce::String& s);
    void setPlayhead (double fraction);
    void setPlaceholder (bool isPlaceholder);

    std::function<void (bool)> onMute, onSolo, onExportToggle;
    std::function<void (float)> onGain;
    std::function<void (double)> onSeek;
    std::function<void()> onDragOut;

    static constexpr int controlsWidth = 232;
    static constexpr int trailingWidth = 72;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    class DragHandle;

    juce::Rectangle<int> getWaveArea() const;
    void rebuildWavePath();
    void seekFromMouse (const juce::MouseEvent&);
    juce::Colour getColour() const;

    std::optional<StemType> type;
    const WaveformPeaks* peaks = nullptr;
    juce::Path wavePath;
    juce::String subtitle;
    StemMixState mixState;
    bool audible = true, placeholder = false;
    double playhead = 0.0;

    juce::TextButton muteButton { "M" }, soloButton { "S" };
    juce::Slider gainSlider;
    juce::ToggleButton exportToggle;
    std::unique_ptr<DragHandle> dragHandle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemLane)
};

} // namespace stemsplitter
