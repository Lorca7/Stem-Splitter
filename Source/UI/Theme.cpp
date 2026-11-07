#include "Theme.h"

namespace stemsplitter
{

namespace Theme
{
    juce::Colour stemColour (StemType t)
    {
        switch (t)
        {
            case StemType::vocals:   return juce::Colour (0xffff5c8a);
            case StemType::drums:    return juce::Colour (0xffffb547);
            case StemType::bass:     return juce::Colour (0xff5b8cff);
            case StemType::guitar:   return juce::Colour (0xff3ddc97);
            case StemType::piano:    return juce::Colour (0xffb07cff);
            case StemType::other:    return juce::Colour (0xff4dd8e6);
            case StemType::numTypes: break;
        }

        return Colours::textDim;
    }

    juce::Font font (float height, bool bold)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Font monoFont (float height)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
    }

    void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                    juce::Colour colour, float height, juce::Justification just)
    {
        g.setColour (colour);
        g.setFont (font (height, true).withExtraKerningFactor (0.08f));
        g.drawText (text.toUpperCase(), area, just, true);
    }

    juce::String formatTime (double seconds, bool withTenths)
    {
        seconds = juce::jmax (0.0, seconds);
        const int mins = (int) (seconds / 60.0);
        const double secs = seconds - mins * 60.0;

        if (withTenths)
            return juce::String (mins) + ":" + juce::String (secs, 1).paddedLeft ('0', 4);

        return juce::String (mins) + ":" + juce::String ((int) secs).paddedLeft ('0', 2);
    }
}

//==============================================================================
StemSplitterLookAndFeel::StemSplitterLookAndFeel()
{
    using namespace Theme::Colours;

    setColourScheme ({ background, panel, raised, outline, text, accent, text, raised, text });

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::textColourId, text);
    setColour (juce::TextButton::buttonColourId, raised);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, raised);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, raised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Label::textColourId, text);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, juce::Colours::white);
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, outline);
    setColour (juce::TooltipWindow::backgroundColourId, raised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, outline);
    setColour (juce::ScrollBar::thumbColourId, outline);
    setColour (juce::TextEditor::backgroundColourId, raised);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, outline);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, outline);
    setColour (juce::ProgressBar::backgroundColourId, raised);
    setColour (juce::ProgressBar::foregroundColourId, accent);
    setColour (juce::ListBox::backgroundColourId, panel);
}

void StemSplitterLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                    bool highlighted, bool down)
{
    using namespace Theme::Colours;
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto variant = button.getProperties()["variant"].toString();
    const bool enabled = button.isEnabled();

    juce::Colour fill = raised, border = outline;

    if (variant == "primary")
    {
        fill = enabled ? (down ? accent.darker (0.15f) : highlighted ? accentBright : accent) : raised;
        border = juce::Colours::transparentBlack;
    }
    else if (variant == "ghost")
    {
        fill = highlighted ? raisedHover : juce::Colours::transparentBlack;
        border = juce::Colours::transparentBlack;
    }
    else if (variant == "danger")
    {
        fill = highlighted ? danger.withAlpha (0.25f) : raised;
        border = danger.withAlpha (0.5f);
    }
    else
    {
        const auto tint = button.getProperties().contains ("tint") ? juce::Colour::fromString (button.getProperties()["tint"].toString()) : accent;
        fill = button.getToggleState() ? tint.withAlpha (0.2f) : (down ? raised.darker (0.2f) : highlighted ? raisedHover : raised);
        border = button.getToggleState() ? tint.withAlpha (0.75f) : outline;
    }

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 6.0f);

    if (! border.isTransparent())
    {
        g.setColour (border);
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
    }
}

juce::Font StemSplitterLookAndFeel::getTextButtonFont (juce::TextButton& b, int buttonHeight)
{
    const bool primary = b.getProperties()["variant"].toString() == "primary";
    return Theme::font (juce::jmin (14.0f, buttonHeight * 0.42f), primary);
}

void StemSplitterLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    using namespace Theme::Colours;
    const auto variant = button.getProperties()["variant"].toString();
    auto colour = variant == "primary" ? juce::Colours::white : text;

    if (button.getToggleState() && button.getProperties().contains ("tint"))
        colour = juce::Colour::fromString (button.getProperties()["tint"].toString()).brighter (0.2f);

    if (! button.isEnabled())
        colour = textFaint;

    g.setColour (colour);
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (8, 0), juce::Justification::centred, 1);
}

void StemSplitterLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    const float box = 16.0f;
    auto bounds = button.getLocalBounds();
    drawTickBox (g, button, 0.0f, (bounds.getHeight() - box) * 0.5f, box, box,
                 button.getToggleState(), button.isEnabled(), highlighted, down);

    g.setColour (button.isEnabled() ? Theme::Colours::text : Theme::Colours::textFaint);
    g.setFont (Theme::font (13.0f));
    g.drawFittedText (button.getButtonText(), bounds.withTrimmedLeft ((int) box + 8), juce::Justification::centredLeft, 1);
}

void StemSplitterLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& c, float x, float y, float w, float h,
                                           bool ticked, bool enabled, bool highlighted, bool)
{
    using namespace Theme::Colours;
    juce::Rectangle<float> r (x, y, w, h);
    auto tint = c.getProperties().contains ("tint") ? juce::Colour::fromString (c.getProperties()["tint"].toString()) : accent;

    if (ticked)
    {
        g.setColour (enabled ? tint : textFaint);
        g.fillRoundedRectangle (r, 4.0f);

        juce::Path tick;
        tick.startNewSubPath (r.getX() + w * 0.26f, r.getCentreY() + h * 0.02f);
        tick.lineTo (r.getX() + w * 0.43f, r.getY() + h * 0.70f);
        tick.lineTo (r.getRight() - w * 0.24f, r.getY() + h * 0.30f);
        g.setColour (background);
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        g.setColour (highlighted ? raisedHover : raised);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (highlighted ? textFaint : outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    }
}

void StemSplitterLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace Theme::Colours;
    juce::Rectangle<float> r (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (box.isMouseOver (true) ? raisedHover : raised);
    g.fillRoundedRectangle (r.reduced (0.5f), 6.0f);
    g.setColour (box.hasKeyboardFocus (false) ? accent.withAlpha (0.6f) : outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    juce::Path chevron;
    const float cx = (float) width - 14.0f, cy = (float) height * 0.5f;
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (textDim);
    g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font StemSplitterLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return Theme::font (13.0f);
}

void StemSplitterLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void StemSplitterLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (Theme::Colours::raised);
    g.setColour (Theme::Colours::outline);
    g.drawRect (0, 0, width, height);
}

void StemSplitterLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                                 bool isActive, bool isHighlighted, bool isTicked, bool,
                                                 const juce::String& text, const juce::String& shortcutKeyText,
                                                 const juce::Drawable*, const juce::Colour*)
{
    using namespace Theme::Colours;

    if (isSeparator)
    {
        g.setColour (outline);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (4, 1);

    if (isHighlighted && isActive)
    {
        g.setColour (accent.withAlpha (0.22f));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
    }

    if (isTicked)
    {
        g.setColour (accentBright);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) r.getX() + 12.0f, (float) r.getCentreY() }));
    }

    g.setColour (isActive ? Theme::Colours::text : textFaint);
    g.setFont (getPopupMenuFont());
    g.drawFittedText (text, r.withTrimmedLeft (24).withTrimmedRight (8), juce::Justification::centredLeft, 1);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (textFaint);
        g.drawText (shortcutKeyText, r.withTrimmedRight (10), juce::Justification::centredRight);
    }
}

juce::Font StemSplitterLookAndFeel::getPopupMenuFont()
{
    return Theme::font (13.5f);
}

void StemSplitterLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                                float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    using namespace Theme::Colours;

    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, slider);
        return;
    }

    const float trackH = 3.0f;
    const float cy = (float) y + (float) height * 0.5f;
    juce::Rectangle<float> track ((float) x, cy - trackH * 0.5f, (float) width, trackH);
    const auto tint = slider.findColour (juce::Slider::trackColourId);

    g.setColour (outline);
    g.fillRoundedRectangle (track, trackH * 0.5f);

    // Fill from the "origin" (0 dB for gain sliders) towards the thumb
    const double origin = slider.getProperties().contains ("origin") ? (double) slider.getProperties()["origin"] : slider.getMinimum();
    const float originX = (float) slider.getPositionOfValue (origin) + (float) x - (float) slider.getLocalBounds().getX();
    const float left = juce::jmin (originX, sliderPos), right = juce::jmax (originX, sliderPos);

    g.setColour (slider.isEnabled() ? tint : textFaint);
    g.fillRoundedRectangle ({ left, track.getY(), right - left, trackH }, trackH * 0.5f);

    const float thumb = slider.isMouseOverOrDragging() ? 12.0f : 10.0f;
    g.setColour (slider.isEnabled() ? Theme::Colours::text : textFaint);
    g.fillEllipse (juce::Rectangle<float> (thumb, thumb).withCentre ({ sliderPos, cy }));
}

void StemSplitterLookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar&, int width, int height,
                                               double progress, const juce::String&)
{
    juce::Rectangle<float> r (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (Theme::Colours::raised);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);

    if (progress >= 0.0 && progress <= 1.0)
    {
        g.setColour (Theme::Colours::accent);
        g.fillRoundedRectangle (r.withWidth (juce::jmax (r.getHeight(), r.getWidth() * (float) progress)), r.getHeight() * 0.5f);
    }
}

juce::Rectangle<int> StemSplitterLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                                juce::Rectangle<int> parentArea)
{
    juce::AttributedString s;
    s.append (tipText, Theme::font (12.5f), Theme::Colours::text);
    juce::TextLayout tl;
    tl.createLayout (s, 280.0f);

    const int w = (int) tl.getWidth() + 20, h = (int) tl.getHeight() + 14;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 16,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8) : screenPos.y + 18,
                                 w, h).constrainedWithin (parentArea);
}

void StemSplitterLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    juce::Rectangle<float> r (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (Theme::Colours::raisedHover);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Theme::Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    juce::AttributedString s;
    s.append (text, Theme::font (12.5f), Theme::Colours::text);
    juce::TextLayout tl;
    tl.createLayout (s, (float) width - 20.0f);
    tl.draw (g, r.reduced (10.0f, 7.0f));
}

void StemSplitterLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                             bool vertical, int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    juce::Rectangle<int> thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize)
                                          : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
    g.setColour (Theme::Colours::outline.brighter (isMouseDown ? 0.4f : isMouseOver ? 0.2f : 0.0f));
    g.fillRoundedRectangle (thumb.reduced (2).toFloat(), 3.0f);
}

void StemSplitterLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    g.setColour (Theme::Colours::raised);
    g.fillPath (path);
    g.setColour (Theme::Colours::outline);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

void StemSplitterLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour (Theme::Colours::raised);
    g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 6.0f);
}

void StemSplitterLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.hasKeyboardFocus (true) ? Theme::Colours::accent : Theme::Colours::outline);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, 6.0f, 1.0f);
}

//==============================================================================
IconButton::IconButton (const juce::String& name, juce::Path p)
    : juce::Button (name), icon (std::move (p))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace Theme::Colours;
    auto bounds = getLocalBounds().toFloat();
    const bool on = getToggleState();

    if (filled || highlighted || on)
    {
        auto fill = filled ? (on ? onColour : raised) : (on ? onColour.withAlpha (0.18f) : raisedHover);

        if (filled && highlighted)
            fill = fill.brighter (0.08f);

        if (down)
            fill = fill.darker (0.1f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds.reduced (0.5f), 6.0f);

        if (filled && ! on)
        {
            g.setColour (outline);
            g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
        }
    }

    auto content = bounds.reduced (6.0f);
    juce::Colour fg = ! isEnabled() ? textFaint
                    : on ? (filled ? background : onColour)
                    : highlighted ? Theme::Colours::text : textDim;

    if (text.isNotEmpty())
    {
        g.setColour (fg);
        g.setFont (Theme::font (12.5f, true));

        if (icon.isEmpty())
        {
            g.drawText (text, content, juce::Justification::centred);
            return;
        }

        const float iconSize = content.getHeight() * 0.8f;
        auto iconArea = content.removeFromLeft (iconSize + 4.0f).withSizeKeepingCentre (iconSize, iconSize);
        g.fillPath (icon, icon.getTransformToScaleToFit (iconArea, true));
        g.drawText (text, content.withTrimmedLeft (2.0f), juce::Justification::centredLeft);
        return;
    }

    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight()) * iconScale;
    g.setColour (fg);
    g.fillPath (icon, icon.getTransformToScaleToFit (bounds.withSizeKeepingCentre (size, size), true));
}

//==============================================================================
void SegmentedControl::setOptions (const juce::StringArray& newOptions, const juce::StringArray& tooltips)
{
    options = newOptions;
    tips = tooltips;
    badges.clear();

    for (int i = 0; i < options.size(); ++i)
        badges.add ({});

    repaint();
}

void SegmentedControl::setOptionBadge (int index, const juce::String& badge)
{
    if (juce::isPositiveAndBelow (index, badges.size()) && badges[index] != badge)
    {
        badges.set (index, badge);
        repaint();
    }
}

void SegmentedControl::setSelectedIndex (int index, juce::NotificationType n)
{
    index = juce::jlimit (0, juce::jmax (0, options.size() - 1), index);

    if (index == selected)
        return;

    selected = index;
    repaint();

    if (n != juce::dontSendNotification && onChange)
        onChange (selected);
}

int SegmentedControl::getIdealWidth() const
{
    int w = 6;
    const auto f = Theme::font (12.5f, true);

    for (int i = 0; i < options.size(); ++i)
        w += juce::GlyphArrangement::getStringWidthInt (f, options[i]) + 28
             + (badges[i].isNotEmpty() ? juce::GlyphArrangement::getStringWidthInt (Theme::font (10.0f, true), badges[i]) + 12 : 0);

    return w;
}

juce::Rectangle<float> SegmentedControl::segmentBounds (int index) const
{
    // Width proportional to each label so it looks balanced
    auto area = getLocalBounds().toFloat().reduced (3.0f);
    const auto f = Theme::font (12.5f, true);
    float total = 0.0f;
    juce::Array<float> widths;

    for (int i = 0; i < options.size(); ++i)
    {
        const float w = (float) juce::GlyphArrangement::getStringWidthInt (f, options[i]) + 28.0f
                      + (badges[i].isNotEmpty() ? (float) juce::GlyphArrangement::getStringWidthInt (Theme::font (10.0f, true), badges[i]) + 12.0f : 0.0f);
        widths.add (w);
        total += w;
    }

    const float scale = total > 0.0f ? area.getWidth() / total : 1.0f;
    float x = area.getX();

    for (int i = 0; i < index; ++i)
        x += widths[i] * scale;

    return { x, area.getY(), widths[index] * scale, area.getHeight() };
}

int SegmentedControl::indexAt (juce::Point<int> p) const
{
    for (int i = 0; i < options.size(); ++i)
        if (segmentBounds (i).contains (p.toFloat()))
            return i;

    return -1;
}

void SegmentedControl::paint (juce::Graphics& g)
{
    using namespace Theme::Colours;
    auto bounds = getLocalBounds().toFloat();

    g.setColour (panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (outlineSoft);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    for (int i = 0; i < options.size(); ++i)
    {
        auto r = segmentBounds (i);
        const bool isSelected = i == selected;

        if (isSelected)
        {
            g.setColour (raisedHover);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (outline.brighter (0.15f));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        }
        else if (i == hovered && isEnabled())
        {
            g.setColour (raised);
            g.fillRoundedRectangle (r, 6.0f);
        }

        auto content = r.toNearestInt();

        if (badges[i].isNotEmpty())
        {
            const auto bf = Theme::font (10.0f, true);
            const int bw = juce::GlyphArrangement::getStringWidthInt (bf, badges[i]) + 8;
            const int textW = juce::GlyphArrangement::getStringWidthInt (Theme::font (12.5f, true), options[i]);
            const int totalW = textW + 4 + bw;
            auto row = content.withSizeKeepingCentre (totalW, content.getHeight());
            auto textArea = row.removeFromLeft (textW);
            row.removeFromLeft (4);
            auto badgeArea = row.withSizeKeepingCentre (bw, 15).toFloat();

            g.setColour (isSelected ? Theme::Colours::text : textDim);
            g.setFont (Theme::font (12.5f, true));
            g.drawText (options[i], textArea, juce::Justification::centred);

            g.setColour (warning.withAlpha (0.16f));
            g.fillRoundedRectangle (badgeArea, 4.0f);
            g.setColour (warning);
            g.setFont (bf);
            g.drawText (badges[i], badgeArea, juce::Justification::centred);
        }
        else
        {
            g.setColour (isSelected ? Theme::Colours::text : textDim);
            g.setFont (Theme::font (12.5f, true));
            g.drawText (options[i], content, juce::Justification::centred);
        }
    }
}

void SegmentedControl::mouseMove (const juce::MouseEvent& e)
{
    const int h = indexAt (e.getPosition());

    if (h != hovered)
    {
        hovered = h;
        repaint();
    }
}

void SegmentedControl::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;

    const int i = indexAt (e.getPosition());

    if (i >= 0)
        setSelectedIndex (i);
}

juce::String SegmentedControl::getTooltip()
{
    const auto pos = getMouseXYRelative();
    const int i = indexAt (pos);
    return juce::isPositiveAndBelow (i, tips.size()) ? tips[i] : juce::String();
}

} // namespace stemsplitter
