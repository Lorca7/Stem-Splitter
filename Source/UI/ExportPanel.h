#pragma once

#include "FrequencyCutView.h"
#include "PluginProcessor.h"

namespace stemsplitter
{

/** Right-hand panel: choose between exporting separate stems or one custom mix (with instruments
    and/or a frequency range removed), pick the format, and export. */
class ExportPanel final : public juce::Component
{
public:
    explicit ExportPanel (StemSplitterProcessor& p);
    ~ExportPanel() override;

    /** Re-reads everything from the processor. */
    void refresh();
    void setBusy (bool isBusy, float progress = 0.0f);

    std::function<void()> onExport;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class RemoveChip;

    void setMode (ExportMode m);
    void applyPreset (const juce::Array<StemType>& keep);
    juce::String getSummary() const;

    StemSplitterProcessor& processor;

    SegmentedControl modeSelector;
    juce::OwnedArray<juce::ToggleButton> stemChecks;
    juce::TextButton selectAll { "All" }, selectNone { "None" };

    juce::OwnedArray<RemoveChip> removeChips;
    juce::OwnedArray<juce::TextButton> presets;

    juce::ToggleButton cutEnabled { "Remove frequencies" };
    SegmentedControl cutMode;
    FrequencyCutView cutView;
    juce::Slider cutAmount;

    juce::ComboBox bitDepthBox, sampleRateBox;
    juce::TextButton exportButton;

    juce::Rectangle<int> stemsSection, removeSection, cutSection, formatSection, summaryArea;
    bool busy = false;
    float busyProgress = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportPanel)
};

} // namespace stemsplitter
