#pragma once

#include "Theme.h"

namespace stemsplitter
{

/** The empty-state "drop a WAV file here" target. The editor handles the actual drag-and-drop for the
    whole window and tells this component when to highlight. */
class DropZone final : public juce::Component
{
public:
    DropZone();

    void setDragHighlight (bool shouldHighlight);
    void setLoading (bool isLoading);

    std::function<void()> onBrowse;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool dragOver = false, loading = false;
};

} // namespace stemsplitter
