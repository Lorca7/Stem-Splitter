#include "DropZone.h"
#include "Icons.h"

namespace stemsplitter
{

DropZone::DropZone()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("Drop an audio file here, or click to choose one");
}

void DropZone::setDragHighlight (bool shouldHighlight)
{
    if (dragOver != shouldHighlight)
    {
        dragOver = shouldHighlight;
        repaint();
    }
}

void DropZone::setLoading (bool isLoading)
{
    if (loading != isLoading)
    {
        loading = isLoading;
        repaint();
    }
}

void DropZone::mouseUp (const juce::MouseEvent& e)
{
    if (! loading && e.mouseWasClicked() && onBrowse)
        onBrowse();
}

void DropZone::paint (juce::Graphics& g)
{
    using namespace Theme::Colours;
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const bool hover = isMouseOver (true);
    const auto tint = dragOver ? accent : hover ? textFaint : outline;

    g.setColour (dragOver ? accent.withAlpha (0.08f) : panel);
    g.fillRoundedRectangle (bounds, 14.0f);

    // dashed border
    juce::Path border;
    border.addRoundedRectangle (bounds.reduced (0.5f), 14.0f);
    juce::Path dashed;
    const float dashes[] = { 6.0f, 6.0f };
    juce::PathStrokeType (1.5f).createDashedStroke (dashed, border, dashes, 2);
    g.setColour (tint);
    g.fillPath (dashed);

    // stem-coloured glyph
    auto centre = bounds.getCentre();
    const float glyphW = 84.0f;
    const auto heights = std::array<float, 6> { 22.0f, 40.0f, 58.0f, 34.0f, 48.0f, 26.0f };
    const auto stemsInOrder = std::array<StemType, 6> { StemType::vocals, StemType::drums, StemType::bass,
                                                        StemType::guitar, StemType::piano, StemType::other };
    const float barW = 8.0f, gap = (glyphW - 6.0f * barW) / 5.0f;
    const float glyphTop = centre.y - 70.0f;

    for (size_t i = 0; i < heights.size(); ++i)
    {
        const float x = centre.x - glyphW * 0.5f + (float) i * (barW + gap);
        const float h = heights[i] * (dragOver ? 1.12f : 1.0f);
        g.setColour (Theme::stemColour (stemsInOrder[i]).withAlpha (dragOver || hover ? 1.0f : 0.85f));
        g.fillRoundedRectangle (x, glyphTop + 30.0f - h * 0.5f, barW, h, barW * 0.5f);
    }

    auto textArea = juce::Rectangle<float> (bounds.getX(), glyphTop + 76.0f, bounds.getWidth(), 28.0f);

    g.setColour (Theme::Colours::text);
    g.setFont (Theme::font (20.0f, true));
    g.drawText (loading ? "Opening..." : dragOver ? "Drop to open" : "Drop a WAV file here", textArea, juce::Justification::centred);

    g.setColour (textDim);
    g.setFont (Theme::font (13.5f));
    g.drawText (loading ? "Decoding audio" : "or click to browse your files", textArea.translated (0.0f, 28.0f),
                juce::Justification::centred);

    g.setColour (textFaint);
    g.setFont (Theme::font (12.0f));
    g.drawText (juce::String::fromUTF8 ("WAV  ·  AIFF  ·  FLAC  ·  MP3"), textArea.translated (0.0f, 62.0f), juce::Justification::centred);
}

} // namespace stemsplitter
