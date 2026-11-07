#pragma once

#include "Theme.h"
#include "PluginProcessor.h"

namespace stemsplitter
{

/** Top bar: logo, engine picker, model status/download, open + settings buttons. */
class HeaderBar final : public juce::Component
{
public:
    explicit HeaderBar (StemSplitterProcessor& p);
    ~HeaderBar() override;

    void refresh();

    std::function<void()> onOpenFile, onSettings, onDownloadModel, onCancelDownload;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class StatusChip;

    StemSplitterProcessor& processor;
    SegmentedControl engineSelector;
    std::unique_ptr<StatusChip> status;
    std::unique_ptr<IconButton> openButton, settingsButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
};

} // namespace stemsplitter
