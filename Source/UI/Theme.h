#pragma once

#include "Core/StemTypes.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace stemsplitter
{

/** Colours, type and shared drawing helpers for the minimal dark UI. */
namespace Theme
{
    namespace Colours
    {
        const juce::Colour background   { 0xff0c0d10 };
        const juce::Colour panel        { 0xff14161a };
        const juce::Colour raised       { 0xff1b1e23 };
        const juce::Colour raisedHover  { 0xff23272e };
        const juce::Colour outline      { 0xff272b32 };
        const juce::Colour outlineSoft  { 0xff1e2127 };
        const juce::Colour text         { 0xffeceef2 };
        const juce::Colour textDim      { 0xff9aa0ab };
        const juce::Colour textFaint    { 0xff5d636e };
        const juce::Colour accent       { 0xff7c6cff };
        const juce::Colour accentBright { 0xff9a8dff };
        const juce::Colour success      { 0xff3ddc97 };
        const juce::Colour warning      { 0xffffb547 };
        const juce::Colour danger       { 0xffff5c6c };
    }

    juce::Colour stemColour (StemType t);

    juce::Font font (float height, bool bold = false);
    juce::Font monoFont (float height);

    constexpr float cornerRadius = 8.0f;
    constexpr int gutter = 16;

    /** Lays out text with letter spacing (used for small caps labels). */
    void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                    juce::Colour colour, float height = 11.0f, juce::Justification just = juce::Justification::centredLeft);

    juce::String formatTime (double seconds, bool withTenths = false);
}

//==============================================================================
class StemSplitterLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    StemSplitterLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h,
                      bool ticked, bool enabled, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 6; }

    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int width, int height, double progress, const juce::String&) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override { return 8; }

    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
};

//==============================================================================
/** A flat button that draws a vector icon, optionally with a label, with hover/press/toggle states. */
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, juce::Path icon);

    void setIcon (juce::Path newIcon)               { icon = std::move (newIcon); repaint(); }
    void setText (const juce::String& t)            { text = t; repaint(); }
    void setOnColour (juce::Colour c)               { onColour = c; repaint(); }
    void setFilledBackground (bool shouldFill)      { filled = shouldFill; repaint(); }
    void setIconScale (float s)                     { iconScale = s; repaint(); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Path icon;
    juce::String text;
    juce::Colour onColour = Theme::Colours::accent;
    bool filled = false;
    float iconScale = 0.5f;
};

//==============================================================================
/** A row of mutually exclusive options ("pills"), e.g. 4 Stems | 6 Stems | Lite. */
class SegmentedControl : public juce::Component, public juce::SettableTooltipClient
{
public:
    SegmentedControl() = default;

    void setOptions (const juce::StringArray& options, const juce::StringArray& tooltips = {});
    void setSelectedIndex (int index, juce::NotificationType = juce::sendNotification);
    int getSelectedIndex() const noexcept { return selected; }
    void setOptionBadge (int index, const juce::String& badge);

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    int getIdealWidth() const;

private:
    int indexAt (juce::Point<int>) const;
    juce::Rectangle<float> segmentBounds (int index) const;

    juce::StringArray options, tips, badges;
    int selected = 0, hovered = -1;
};

} // namespace stemsplitter
