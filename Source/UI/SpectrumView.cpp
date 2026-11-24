#include "SpectrumView.h"

namespace stemsplitter
{

SpectrumView::SpectrumView()
{
    setTooltip ("Click a peak to remove that frequency. Drag a marker to move it; double-click it to remove it.");
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

void SpectrumView::setData (std::shared_ptr<const CleanupAnalysis> a, const CleanupSettings& s, float hum)
{
    analysis = std::move (a);
    settings = s;
    humHz = hum;

    if (draggingNotch < 0)
        notches = s.notchesHz;

    repaint();
}

juce::Rectangle<float> SpectrumView::plotArea() const
{
    return getLocalBounds().toFloat().reduced (8.0f, 6.0f).withTrimmedBottom (12.0f);
}

float SpectrumView::xFor (float hz) const
{
    const auto r = plotArea();
    return r.getX() + r.getWidth() * std::log (juce::jlimit (20.0f, 20000.0f, hz) / 20.0f) / std::log (1000.0f);
}

float SpectrumView::hzFor (float x) const
{
    const auto r = plotArea();
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - r.getX()) / juce::jmax (1.0f, r.getWidth())));
}

float SpectrumView::yFor (float db) const
{
    const auto r = plotArea();
    return r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, (topDb - db) / (topDb - bottomDb));
}

int SpectrumView::notchAt (float x) const
{
    int best = -1;
    float bestDist = 7.0f;

    for (size_t i = 0; i < notches.size(); ++i)
    {
        const float d = std::abs (xFor (notches[i]) - x);
        if (d < bestDist)
        {
            bestDist = d;
            best = (int) i;
        }
    }

    return best;
}

void SpectrumView::paint (juce::Graphics& g)
{
    using namespace Theme::Colours;
    const auto bounds = getLocalBounds().toFloat();
    const auto r = plotArea();

    g.setColour (background);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (outlineSoft);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    g.setFont (Theme::font (9.5f));

    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xFor (hz);
        g.setColour (outlineSoft);
        g.fillRect (x, r.getY(), 1.0f, r.getHeight());

        if (juce::exactlyEqual (hz, 100.0f) || juce::exactlyEqual (hz, 1000.0f) || juce::exactlyEqual (hz, 10000.0f))
        {
            g.setColour (textFaint);
            g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                        juce::Rectangle<float> (x - 20.0f, r.getBottom() + 1.0f, 40.0f, 11.0f), juce::Justification::centred);
        }
    }

    if (analysis == nullptr || ! analysis->isValid())
    {
        g.setColour (textFaint);
        g.setFont (Theme::font (12.0f));
        g.drawText (analysis == nullptr ? "Analysing..." : "No audio", r, juce::Justification::centred);
        return;
    }

    const int steps = juce::jmax (2, (int) r.getWidth());
    auto curve = [&] (const std::vector<float>& db)
    {
        juce::Path p;

        for (int i = 0; i <= steps; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) steps;
            const float hz0 = hzFor (x - 0.5f), hz1 = hzFor (x + 0.5f);

            // max over the bins this pixel covers, so narrow peaks stay visible
            float v = -200.0f;
            const double binHz = analysis->sampleRate / analysis->displayFftSize;
            const int k0 = juce::jmax (0, (int) (hz0 / binHz)), k1 = juce::jmin ((int) db.size() - 1, (int) std::ceil (hz1 / binHz));
            for (int k = k0; k <= k1; ++k)
                v = juce::jmax (v, db[(size_t) k]);

            const float y = yFor (v);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }

        return p;
    };

    // average spectrum (filled)
    auto avg = curve (analysis->averageDb);
    juce::Path fill (avg);
    fill.lineTo (r.getRight(), r.getBottom());
    fill.lineTo (r.getX(), r.getBottom());
    fill.closeSubPath();
    g.setColour (textDim.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (textDim.withAlpha (0.45f));
    g.strokePath (avg, juce::PathStrokeType (1.0f));

    // steady noise floor
    g.setColour (warning.withAlpha (0.85f));
    g.strokePath (curve (analysis->floorDb), juce::PathStrokeType (1.2f));

    // removed regions from the hum/notch/rumble filters (drawn from the top)
    if (settings.enabled)
    {
        auto drawSettings = settings;
        drawSettings.notchesHz = notches;
        juce::Path removed;
        removed.startNewSubPath (r.getX(), r.getY());

        for (int i = 0; i <= steps; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) steps;
            const float att = NoiseCleanup::getFilterResponseDb (drawSettings, humHz, analysis->sampleRate, hzFor (x));
            removed.lineTo (x, r.getY() + juce::jmin (r.getHeight(), -att / 60.0f * r.getHeight()));
        }

        removed.lineTo (r.getRight(), r.getY());
        removed.closeSubPath();
        g.setColour (danger.withAlpha (0.22f));
        g.fillPath (removed);
    }

    // hum harmonics
    if (settings.enabled && settings.dehum && humHz > 0.0f)
    {
        g.setColour (danger.withAlpha (0.7f));
        for (int h = 1; h <= settings.humHarmonics; ++h)
        {
            const float x = xFor (humHz * (float) h);
            if (x > r.getRight())
                break;
            g.fillRect (x - 0.5f, r.getBottom() - 6.0f, 1.0f, 6.0f);
        }
    }

    // notch markers
    for (size_t i = 0; i < notches.size(); ++i)
    {
        const float x = xFor (notches[i]);
        const bool hot = (int) i == hoveredNotch || (int) i == draggingNotch;
        g.setColour ((settings.enabled ? danger : textDim).withAlpha (hot ? 1.0f : 0.8f));
        g.fillRect (x - 0.75f, r.getY(), 1.5f, r.getHeight());
        g.fillEllipse (x - 4.0f, r.getY() - 1.0f, 8.0f, 8.0f);

        if (hot)
        {
            auto box = juce::Rectangle<float> (x + 6.0f, r.getY() + 4.0f, 60.0f, 16.0f);
            if (box.getRight() > r.getRight()) box.setX (x - 66.0f);
            g.setColour (raisedHover);
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (Theme::Colours::text);
            g.setFont (Theme::font (10.5f, true));
            g.drawText (juce::String (juce::roundToInt (notches[i])) + " Hz", box, juce::Justification::centred);
        }
    }

    // hover readout for adding a notch
    if (hovering && hoveredNotch < 0 && draggingNotch < 0 && isEnabled())
    {
        const float x = xFor (hoverHz);
        g.setColour (Theme::Colours::text.withAlpha (0.35f));
        g.fillRect (x - 0.5f, r.getY(), 1.0f, r.getHeight());
        auto box = juce::Rectangle<float> (x + 6.0f, r.getBottom() - 22.0f, 74.0f, 16.0f);
        if (box.getRight() > r.getRight()) box.setX (x - 80.0f);
        g.setColour (raisedHover);
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (Theme::Colours::text);
        g.setFont (Theme::font (10.5f));
        g.drawText ("+ " + juce::String (juce::roundToInt (hoverHz)) + " Hz", box, juce::Justification::centred);
    }
}

void SpectrumView::mouseMove (const juce::MouseEvent& e)
{
    hovering = true;
    hoveredNotch = notchAt (e.position.x);
    hoverHz = analysis != nullptr ? NoiseCleanup::snapToPeak (*analysis, hzFor (e.position.x)) : hzFor (e.position.x);
    setMouseCursor (hoveredNotch >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::CrosshairCursor);
    repaint();
}

void SpectrumView::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    hoveredNotch = -1;
    repaint();
}

void SpectrumView::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled() || analysis == nullptr)
        return;

    const int hit = notchAt (e.position.x);

    if (e.mods.isPopupMenu())
    {
        if (hit >= 0)
        {
            notches.erase (notches.begin() + hit);
            hoveredNotch = -1;
            commit();
        }
        return;
    }

    if (hit >= 0)
    {
        draggingNotch = hit;
        return;
    }

    if ((int) notches.size() >= CleanupSettings::maxNotches)
        return;

    notches.push_back (NoiseCleanup::snapToPeak (*analysis, hzFor (e.position.x)));
    draggingNotch = (int) notches.size() - 1;
    hoveredNotch = draggingNotch;
    repaint();
}

void SpectrumView::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingNotch < 0 || e.getDistanceFromDragStart() < 3)
        return;

    notches[(size_t) draggingNotch] = hzFor (e.position.x);
    repaint();
}

void SpectrumView::mouseUp (const juce::MouseEvent& e)
{
    if (draggingNotch < 0)
        return;

    // A drag that ends on a peak snaps to it precisely
    if (e.getDistanceFromDragStart() >= 3 && analysis != nullptr)
        notches[(size_t) draggingNotch] = NoiseCleanup::snapToPeak (*analysis, notches[(size_t) draggingNotch], 0.03f);

    draggingNotch = -1;
    commit();
}

void SpectrumView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int hit = notchAt (e.position.x);

    if (hit >= 0)
    {
        notches.erase (notches.begin() + hit);
        hoveredNotch = -1;
        commit();
    }
}

void SpectrumView::commit()
{
    std::sort (notches.begin(), notches.end());
    repaint();

    if (onNotchesChanged)
        onNotchesChanged (notches);
}

} // namespace stemsplitter
