#include "StemLane.h"
#include "Icons.h"

namespace stemsplitter
{

class StemLane::DragHandle final : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragHandle()
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag this stem into Finder or straight into your DAW");
    }

    std::function<void()> onDragOut;

    void paint (juce::Graphics& g) override
    {
        const bool hover = isMouseOver (true);

        if (hover)
        {
            g.setColour (Theme::Colours::raisedHover);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        }

        const auto icon = Icons::dragHandle();
        g.setColour (hover ? Theme::Colours::text : Theme::Colours::textFaint);
        g.fillPath (icon, icon.getTransformToScaleToFit (getLocalBounds().toFloat().withSizeKeepingCentre (18.0f, 18.0f), true));
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseDown (const juce::MouseEvent&) override  { started = false; }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! started && e.getDistanceFromDragStart() > 4 && onDragOut)
        {
            started = true;
            onDragOut();
        }
    }

private:
    bool started = false;
};

//==============================================================================
StemLane::StemLane (std::optional<StemType> t) : type (t)
{
    if (type.has_value())
    {
        const auto colour = getColour();

        for (auto* b : { &muteButton, &soloButton })
        {
            b->setClickingTogglesState (true);
            b->getProperties().set ("variant", "toggle");
            addAndMakeVisible (*b);
        }

        muteButton.getProperties().set ("tint", Theme::Colours::danger.toString());
        soloButton.getProperties().set ("tint", Theme::Colours::warning.toString());
        muteButton.setTooltip ("Mute: remove this stem from the preview and the mix export");
        soloButton.setTooltip ("Solo: hear only soloed stems");
        muteButton.onClick = [this] { if (onMute) onMute (muteButton.getToggleState()); };
        soloButton.onClick = [this] { if (onSolo) onSolo (soloButton.getToggleState()); };

        gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        gainSlider.setRange (-36.0, 12.0, 0.1);
        gainSlider.setSkewFactorFromMidPoint (-6.0);
        gainSlider.setDoubleClickReturnValue (true, 0.0);
        gainSlider.setValue (0.0, juce::dontSendNotification);
        gainSlider.getProperties().set ("origin", 0.0);
        gainSlider.setColour (juce::Slider::trackColourId, colour);
        gainSlider.setPopupDisplayEnabled (true, true, nullptr);
        gainSlider.setTextValueSuffix (" dB");
        gainSlider.setTooltip ("Level (double-click to reset)");
        gainSlider.onValueChange = [this]
        {
            if (onGain) onGain ((float) gainSlider.getValue());
            repaint();
        };
        addAndMakeVisible (gainSlider);

        exportToggle.getProperties().set ("tint", colour.toString());
        exportToggle.setTooltip ("Include this stem when exporting stems");
        exportToggle.onClick = [this] { if (onExportToggle) onExportToggle (exportToggle.getToggleState()); };
        addAndMakeVisible (exportToggle);

        dragHandle = std::make_unique<DragHandle>();
        dragHandle->onDragOut = [this] { if (onDragOut) onDragOut(); };
        addAndMakeVisible (*dragHandle);
    }

    setMouseCursor (juce::MouseCursor::NormalCursor);
}

StemLane::~StemLane() = default;

juce::Colour StemLane::getColour() const
{
    return type.has_value() ? Theme::stemColour (*type) : Theme::Colours::textDim;
}

void StemLane::setWaveform (const WaveformPeaks* newPeaks)
{
    peaks = newPeaks;
    rebuildWavePath();
    repaint();
}

void StemLane::setMixState (const StemMixState& state, bool isAudible)
{
    mixState = state;
    audible = isAudible;
    muteButton.setToggleState (state.muted, juce::dontSendNotification);
    soloButton.setToggleState (state.solo, juce::dontSendNotification);

    if (! gainSlider.isMouseButtonDown())
        gainSlider.setValue (state.gainDb, juce::dontSendNotification);

    repaint();
}

void StemLane::setExportSelected (bool selected)
{
    exportToggle.setToggleState (selected, juce::dontSendNotification);
}

void StemLane::setShowExportToggle (bool show)
{
    exportToggle.setVisible (show && type.has_value());
}

void StemLane::setSubtitle (const juce::String& s)
{
    subtitle = s;
    repaint();
}

void StemLane::setPlaceholder (bool isPlaceholder)
{
    placeholder = isPlaceholder;

    for (auto* c : std::initializer_list<juce::Component*> { &muteButton, &soloButton, &gainSlider })
        c->setVisible (type.has_value() && ! placeholder);

    exportToggle.setEnabled (! placeholder);

    if (dragHandle != nullptr)
        dragHandle->setVisible (! placeholder);

    repaint();
}

void StemLane::setPlayhead (double fraction)
{
    fraction = juce::jlimit (0.0, 1.0, fraction);
    auto area = getWaveArea();

    const int oldX = area.getX() + (int) (playhead * area.getWidth());
    const int newX = area.getX() + (int) (fraction * area.getWidth());

    if (oldX == newX)
        return;

    playhead = fraction;
    repaint (juce::Rectangle<int>::leftTopRightBottom (juce::jmin (oldX, newX) - 2, area.getY(),
                                                       juce::jmax (oldX, newX) + 3, area.getBottom()));
}

juce::Rectangle<int> StemLane::getWaveArea() const
{
    auto r = getLocalBounds().reduced (0, 8);
    r.removeFromLeft (controlsWidth);
    r.removeFromRight (trailingWidth);   // same for every lane so the time axes line up
    return r;
}

void StemLane::resized()
{
    auto r = getLocalBounds();
    auto controls = r.removeFromLeft (controlsWidth).reduced (16, 0);

    if (type.has_value())
    {
        auto buttons = controls.removeFromRight (62).withSizeKeepingCentre (62, 24);
        muteButton.setBounds (buttons.removeFromLeft (28));
        buttons.removeFromLeft (6);
        soloButton.setBounds (buttons.removeFromLeft (28));

        auto bottom = controls.withTrimmedTop (getHeight() / 2).withTrimmedRight (14).withTrimmedLeft (6);
        gainSlider.setBounds (bottom.withHeight (20).withY (bottom.getY() + 2));

        auto trailing = r.removeFromRight (trailingWidth).reduced (8, 0);
        exportToggle.setBounds (trailing.removeFromLeft (20).withSizeKeepingCentre (20, 20));
        dragHandle->setBounds (trailing.removeFromRight (28).withSizeKeepingCentre (28, 36));
    }

    rebuildWavePath();
}

void StemLane::rebuildWavePath()
{
    wavePath.clear();
    const auto area = getWaveArea().toFloat();

    if (peaks == nullptr || peaks->isEmpty() || area.getWidth() < 2.0f)
        return;

    const double total = peaks->getNumSourceSamples();
    const int columns = (int) area.getWidth();
    const float mid = area.getCentreY();
    const float halfH = area.getHeight() * 0.5f;

    // Normalise gently so quiet stems are still visible, without exaggerating noise.
    const float peak = peaks->getOverallPeak();
    const float scale = peak > 1.0e-4f ? juce::jmin (1.0f / peak, 4.0f) * 0.92f : 1.0f;

    for (int x = 0; x < columns; ++x)
    {
        const auto range = peaks->getRange (total * x / columns, total * (x + 1) / columns);
        const float top = juce::jmax (-1.0f, range.getEnd() * scale);
        const float bottom = juce::jmin (1.0f, range.getStart() * scale);
        const float y0 = mid - top * halfH;
        const float h = juce::jmax (1.0f, (top - bottom) * halfH);
        wavePath.addRectangle (area.getX() + (float) x, y0, 0.85f, h);
    }
}

void StemLane::paint (juce::Graphics& g)
{
    using namespace Theme::Colours;
    auto bounds = getLocalBounds().toFloat();
    const auto colour = getColour();

    g.setColour (panel);
    g.fillRoundedRectangle (bounds, Theme::cornerRadius);

    // colour strip
    g.setColour (colour.withAlpha (audible && ! placeholder ? 1.0f : 0.3f));
    g.fillRoundedRectangle (bounds.getX() + 6.0f, bounds.getY() + 14.0f, 3.0f, bounds.getHeight() - 28.0f, 1.5f);

    // name + subtitle
    auto controls = getLocalBounds().removeFromLeft (controlsWidth).reduced (22, 0);
    const auto name = type.has_value() ? getStemName (*type) : juce::String ("Original");

    g.setColour (placeholder ? textFaint : (audible ? Theme::Colours::text : textDim));
    g.setFont (Theme::font (14.5f, true));

    if (type.has_value())
    {
        g.drawText (name, controls.withHeight (getHeight() / 2).withTrimmedTop (6), juce::Justification::centredLeft);
    }
    else
    {
        g.drawText (name, controls.withHeight (getHeight() / 2).withTrimmedTop (8), juce::Justification::centredLeft);
        g.setColour (textFaint);
        g.setFont (Theme::font (12.0f));
        g.drawText (subtitle, controls.withTrimmedTop (getHeight() / 2).withTrimmedBottom (8), juce::Justification::centredLeft);
    }

    if (type.has_value() && ! placeholder)
    {
        // gain readout next to the slider
        const auto db = mixState.gainDb;
        const auto text = std::abs (db) < 0.05f ? juce::String ("0 dB") : juce::String (db, 1) + " dB";
        g.setColour (textFaint);
        g.setFont (Theme::font (11.0f));
        g.drawText (text, controls.withTrimmedTop (getHeight() / 2).withTrimmedBottom (4).removeFromRight (62).translated (8, 0),
                    juce::Justification::centredRight);
    }

    // waveform
    const auto wave = getWaveArea();

    g.setColour (outlineSoft);
    g.fillRect (wave.getX(), wave.getCentreY(), wave.getWidth(), 1);

    if (placeholder)
    {
        g.setColour (textFaint);
        g.setFont (Theme::font (12.5f));
        g.drawText (subtitle, wave, juce::Justification::centred);
        return;
    }

    if (! wavePath.isEmpty())
    {
        const auto waveColour = audible ? colour : textFaint;
        const int playX = wave.getX() + (int) (playhead * wave.getWidth());

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (juce::Rectangle<int>::leftTopRightBottom (wave.getX(), wave.getY(), playX, wave.getBottom()));
            g.setColour (waveColour.withAlpha (audible ? 0.95f : 0.45f));
            g.fillPath (wavePath);
        }
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (juce::Rectangle<int>::leftTopRightBottom (playX, wave.getY(), wave.getRight(), wave.getBottom()));
            g.setColour (waveColour.withAlpha (audible ? 0.55f : 0.25f));
            g.fillPath (wavePath);
        }
    }

    // playhead
    const float px = (float) wave.getX() + (float) (playhead * wave.getWidth());
    g.setColour (Theme::Colours::text.withAlpha (0.9f));
    g.fillRect (px - 0.5f, (float) wave.getY(), 1.5f, (float) wave.getHeight());
}

void StemLane::seekFromMouse (const juce::MouseEvent& e)
{
    const auto wave = getWaveArea();

    if (onSeek && wave.getWidth() > 0)
        onSeek (juce::jlimit (0.0, 1.0, (double) (e.x - wave.getX()) / (double) wave.getWidth()));
}

void StemLane::mouseDown (const juce::MouseEvent& e)
{
    if (! placeholder && getWaveArea().expanded (0, 8).contains (e.getPosition()))
        seekFromMouse (e);
}

void StemLane::mouseDrag (const juce::MouseEvent& e)
{
    if (! placeholder && getWaveArea().expanded (0, 8).contains (e.getMouseDownPosition()))
        seekFromMouse (e);
}

void StemLane::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (! placeholder && getWaveArea().contains (e.getPosition()) ? juce::MouseCursor::IBeamCursor
                                                                             : juce::MouseCursor::NormalCursor);
}

} // namespace stemsplitter
