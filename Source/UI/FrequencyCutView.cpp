#include "FrequencyCutView.h"

namespace stemsplitter
{

FrequencyCutView::FrequencyCutView()
{
    setTooltip ("Drag the handles to choose what to remove. Drag inside the band to move it, scroll to resize it.");
}

void FrequencyCutView::setSettings (const FrequencyCutSettings& s)
{
    if (s != settings)
    {
        settings = s;
        repaint();
    }
}

void FrequencyCutView::resized() {}

juce::Rectangle<float> FrequencyCutView::getPlotArea() const
{
    return getLocalBounds().toFloat().reduced (10.0f, 8.0f).withTrimmedBottom (14.0f);
}

float FrequencyCutView::xForFrequency (float hz) const
{
    const auto area = getPlotArea();
    const float norm = std::log (juce::jlimit (20.0f, 20000.0f, hz) / 20.0f) / std::log (1000.0f);
    return area.getX() + norm * area.getWidth();
}

float FrequencyCutView::frequencyForX (float x) const
{
    const auto area = getPlotArea();
    const float norm = juce::jlimit (0.0f, 1.0f, (x - area.getX()) / juce::jmax (1.0f, area.getWidth()));
    return 20.0f * std::pow (1000.0f, norm);
}

float FrequencyCutView::yForDb (float db) const
{
    const auto area = getPlotArea();
    return area.getY() + juce::jlimit (0.0f, 1.0f, db / minDb) * area.getHeight();
}

void FrequencyCutView::paint (juce::Graphics& g)
{
    using namespace Theme::Colours;
    const auto bounds = getLocalBounds().toFloat();
    const auto area = getPlotArea();
    const bool active = settings.enabled && isEnabled();

    g.setColour (background);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (outlineSoft);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    // grid
    g.setFont (Theme::font (9.5f));

    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFrequency (hz);
        g.setColour (outlineSoft);
        g.fillRect (x, area.getY(), 1.0f, area.getHeight());

        if (juce::exactlyEqual (hz, 100.0f) || juce::exactlyEqual (hz, 1000.0f) || juce::exactlyEqual (hz, 10000.0f))
        {
            g.setColour (textFaint);
            g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                        juce::Rectangle<float> (x - 20.0f, area.getBottom() + 1.0f, 40.0f, 12.0f), juce::Justification::centred);
        }
    }

    // response curve
    juce::Path curve;
    juce::Path fill;
    const int steps = juce::jmax (2, (int) area.getWidth());

    for (int i = 0; i <= steps; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / (float) steps;
        const float y = yForDb (FrequencyCutProcessor::getMagnitudeDb (settings, frequencyForX (x)));

        if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, area.getY()); fill.lineTo (x, y); }
        else        { curve.lineTo (x, y); fill.lineTo (x, y); }
    }

    fill.lineTo (area.getRight(), area.getY());
    fill.closeSubPath();

    const auto tint = active ? danger : textFaint;
    g.setColour (tint.withAlpha (active ? 0.16f : 0.06f));
    g.fillPath (fill);
    g.setColour (active ? accentBright : textFaint);
    g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // handles
    auto drawHandle = [&] (float hz, Handle h)
    {
        const float x = xForFrequency (hz);
        const bool hot = hovered == h || dragging == h;
        const float r = hot ? 6.5f : 5.0f;
        const float y = area.getY() + 10.0f;

        g.setColour ((active ? accentBright : textDim).withAlpha (hot ? 0.5f : 0.25f));
        g.fillRect (x - 0.5f, area.getY(), 1.0f, area.getHeight());
        g.setColour (active ? Theme::Colours::text : textDim);
        g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);

        if (hot)
        {
            const auto label = FrequencyCutSettings::formatHz (hz);
            const float w = 56.0f;
            auto box = juce::Rectangle<float> (x - w * 0.5f, y + 10.0f, w, 16.0f);
            box = box.withX (juce::jlimit (area.getX(), area.getRight() - w, box.getX()));
            g.setColour (raisedHover);
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (Theme::Colours::text);
            g.setFont (Theme::font (10.5f, true));
            g.drawText (label, box, juce::Justification::centred);
        }
    };

    if (settings.mode != FrequencyCutSettings::Mode::highCut)
        drawHandle (settings.lowHz, Handle::low);

    if (settings.mode != FrequencyCutSettings::Mode::lowCut)
        drawHandle (settings.highHz, Handle::high);
}

FrequencyCutView::Handle FrequencyCutView::handleAt (juce::Point<float> p) const
{
    const float lowX = xForFrequency (settings.lowHz), highX = xForFrequency (settings.highHz);
    const bool hasLow = settings.mode != FrequencyCutSettings::Mode::highCut;
    const bool hasHigh = settings.mode != FrequencyCutSettings::Mode::lowCut;

    const float dLow = hasLow ? std::abs (p.x - lowX) : 1.0e9f;
    const float dHigh = hasHigh ? std::abs (p.x - highX) : 1.0e9f;

    if (juce::jmin (dLow, dHigh) < 10.0f)
        return dLow <= dHigh ? Handle::low : Handle::high;

    if (settings.mode == FrequencyCutSettings::Mode::bandCut && p.x > lowX && p.x < highX)
        return Handle::band;

    // Clicking elsewhere grabs the nearest edge
    return dLow <= dHigh ? Handle::low : Handle::high;
}

void FrequencyCutView::update (FrequencyCutSettings s)
{
    s.lowHz = juce::jlimit (20.0f, 20000.0f, s.lowHz);
    s.highHz = juce::jlimit (20.0f, 20000.0f, s.highHz);

    if (s.mode == FrequencyCutSettings::Mode::bandCut && s.highHz < s.lowHz * 1.26f)
    {
        if (dragging == Handle::low) s.lowHz = s.highHz / 1.26f;
        else                         s.highHz = juce::jmin (20000.0f, s.lowHz * 1.26f);
    }

    if (s != settings)
    {
        settings = s;
        repaint();

        if (onChange)
            onChange (settings);
    }
}

void FrequencyCutView::mouseMove (const juce::MouseEvent& e)
{
    const auto h = isEnabled() ? handleAt (e.position) : Handle::none;

    if (h != hovered)
    {
        hovered = h;
        repaint();
    }

    setMouseCursor (h == Handle::band ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::LeftRightResizeCursor);
}

void FrequencyCutView::mouseExit (const juce::MouseEvent&)
{
    hovered = Handle::none;
    repaint();
}

void FrequencyCutView::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;

    dragging = handleAt (e.position);
    dragStartLow = settings.lowHz;
    dragStartHigh = settings.highHz;

    if (! settings.enabled)
    {
        auto s = settings;
        s.enabled = true;
        update (s);
    }

    mouseDrag (e);
}

void FrequencyCutView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Handle::none)
        return;

    auto s = settings;

    switch (dragging)
    {
        case Handle::low:  s.lowHz = frequencyForX (e.position.x); break;
        case Handle::high: s.highHz = frequencyForX (e.position.x); break;
        case Handle::band:
        {
            const float startX = (float) e.getMouseDownX();
            const float ratio = frequencyForX (e.position.x) / frequencyForX (startX);
            const float clampedRatio = juce::jlimit (20.0f / dragStartLow, 20000.0f / dragStartHigh, ratio);
            s.lowHz = dragStartLow * clampedRatio;
            s.highHz = dragStartHigh * clampedRatio;
            break;
        }
        case Handle::none: break;
    }

    update (s);
}

void FrequencyCutView::mouseUp (const juce::MouseEvent&)
{
    dragging = Handle::none;
    repaint();
}

void FrequencyCutView::mouseDoubleClick (const juce::MouseEvent&)
{
    auto s = settings;
    const FrequencyCutSettings defaults;
    s.lowHz = s.mode == FrequencyCutSettings::Mode::lowCut ? 120.0f : defaults.lowHz;
    s.highHz = s.mode == FrequencyCutSettings::Mode::highCut ? 10000.0f : defaults.highHz;
    update (s);
}

void FrequencyCutView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (! isEnabled() || settings.mode != FrequencyCutSettings::Mode::bandCut)
        return;

    // widen / narrow the band symmetrically (in octaves)
    const float centre = std::sqrt (settings.lowHz * settings.highHz);
    const float widthOct = std::log2 (settings.highHz / settings.lowHz);
    const float newWidth = juce::jlimit (0.34f, 9.0f, widthOct * (1.0f + wheel.deltaY * 0.6f));

    auto s = settings;
    s.lowHz = centre / std::pow (2.0f, newWidth * 0.5f);
    s.highHz = centre * std::pow (2.0f, newWidth * 0.5f);
    update (s);
}

} // namespace stemsplitter
