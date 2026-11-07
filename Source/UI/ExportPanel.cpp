#include "ExportPanel.h"
#include "Icons.h"

namespace stemsplitter
{

namespace
{
    constexpr int sectionGap = 22;
    constexpr int headerHeight = 18;

    const std::array<StemType, numStemTypes> displayOrder { StemType::vocals, StemType::drums, StemType::bass,
                                                            StemType::guitar, StemType::piano, StemType::other };
}

/** A pill you click to remove (mute) an instrument from the mix. */
class ExportPanel::RemoveChip final : public juce::Button
{
public:
    explicit RemoveChip (StemType t) : juce::Button (getStemName (t)), type (t)
    {
        setClickingTogglesState (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Click to remove " + getStemName (t).toLowerCase() + " from the mix");
    }

    StemType type;

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        using namespace Theme::Colours;
        const bool removed = getToggleState();
        const auto colour = Theme::stemColour (type);
        auto r = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (removed ? raised : colour.withAlpha (highlighted ? 0.24f : 0.16f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (removed ? outline : colour.withAlpha (0.6f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

        auto content = r.reduced (10.0f, 0.0f);
        auto dot = content.removeFromLeft (8.0f).withSizeKeepingCentre (7.0f, 7.0f);
        g.setColour (removed ? textFaint : colour);
        g.fillEllipse (dot);

        g.setColour (removed ? textFaint : Theme::Colours::text);
        g.setFont (Theme::font (12.5f, true));
        content.removeFromLeft (6.0f);
        g.drawText (getName(), content, juce::Justification::centredLeft);

        if (removed)
        {
            // strike-through
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (Theme::font (12.5f, true), getName());
            g.setColour (textDim);
            g.fillRect (content.getX() - 1.0f, content.getCentreY(), w + 2.0f, 1.2f);
        }
    }
};

//==============================================================================
ExportPanel::ExportPanel (StemSplitterProcessor& p) : processor (p)
{
    modeSelector.setOptions ({ "Stems", "Custom mix" },
                             { "Export every selected stem as its own WAV file",
                               "Export one WAV with instruments and/or frequencies removed" });
    modeSelector.onChange = [this] (int i) { setMode (i == 0 ? ExportMode::stems : ExportMode::mix); };
    addAndMakeVisible (modeSelector);

    for (auto type : displayOrder)
    {
        auto* check = stemChecks.add (new juce::ToggleButton (getStemName (type)));
        check->getProperties().set ("tint", Theme::stemColour (type).toString());
        check->onClick = [this, type, check]
        {
            auto prefs = processor.getExportPreferences();
            prefs.stemSelected[(size_t) toIndex (type)] = check->getToggleState();
            processor.setExportPreferences (prefs);
        };
        addChildComponent (check);

        auto* chip = removeChips.add (new RemoveChip (type));
        chip->onClick = [this, chip] { processor.setStemMuted (chip->type, chip->getToggleState()); };
        addChildComponent (chip);
    }

    for (auto* b : { &selectAll, &selectNone })
    {
        b->getProperties().set ("variant", "ghost");
        addChildComponent (*b);
    }

    selectAll.onClick = [this]
    {
        auto prefs = processor.getExportPreferences();
        prefs.stemSelected.fill (true);
        processor.setExportPreferences (prefs);
    };

    selectNone.onClick = [this]
    {
        auto prefs = processor.getExportPreferences();
        prefs.stemSelected.fill (false);
        processor.setExportPreferences (prefs);
    };

    struct Preset { const char* name; const char* tip; juce::Array<StemType> keep; };
    const std::vector<Preset> presetList {
        { "Instrumental", "Everything except vocals (karaoke)", { StemType::drums, StemType::bass, StemType::guitar, StemType::piano, StemType::other } },
        { "A cappella",   "Vocals only", { StemType::vocals } },
        { "No drums",     "Everything except drums (drumless practice track)", { StemType::vocals, StemType::bass, StemType::guitar, StemType::piano, StemType::other } },
        { "No bass",      "Everything except bass (bassless practice track)", { StemType::vocals, StemType::drums, StemType::guitar, StemType::piano, StemType::other } },
    };

    for (const auto& preset : presetList)
    {
        auto* b = presets.add (new juce::TextButton (preset.name));
        b->setTooltip (preset.tip);
        const auto keep = preset.keep;
        b->onClick = [this, keep] { applyPreset (keep); };
        addChildComponent (b);
    }

    cutEnabled.setTooltip ("Remove a range of frequencies from the mix (also applies while previewing)");
    cutEnabled.onClick = [this]
    {
        auto cut = processor.getMix().cut;
        cut.enabled = cutEnabled.getToggleState();
        processor.setFrequencyCut (cut);
    };
    addChildComponent (cutEnabled);

    cutMode.setOptions ({ "Low cut", "Band cut", "High cut" },
                        { "Remove everything below a frequency (rumble, bass)",
                          "Remove a band of frequencies between two points",
                          "Remove everything above a frequency (hiss, cymbals)" });
    cutMode.onChange = [this] (int i)
    {
        auto cut = processor.getMix().cut;
        cut.mode = (FrequencyCutSettings::Mode) i;
        cut.enabled = true;

        if (cut.mode == FrequencyCutSettings::Mode::lowCut && cut.lowHz > 2000.0f)  cut.lowHz = 120.0f;
        if (cut.mode == FrequencyCutSettings::Mode::highCut && cut.highHz < 1000.0f) cut.highHz = 10000.0f;

        processor.setFrequencyCut (cut);
    };
    addChildComponent (cutMode);

    cutView.onChange = [this] (const FrequencyCutSettings& s) { processor.setFrequencyCut (s); };
    addChildComponent (cutView);

    cutAmount.setSliderStyle (juce::Slider::LinearHorizontal);
    cutAmount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    cutAmount.setRange (0.0, 100.0, 1.0);
    cutAmount.setDoubleClickReturnValue (true, 100.0);
    cutAmount.setTooltip ("How much of the selected range to remove");
    cutAmount.setPopupDisplayEnabled (true, true, nullptr);
    cutAmount.setTextValueSuffix ("%");
    cutAmount.onValueChange = [this]
    {
        auto cut = processor.getMix().cut;
        cut.amount = (float) cutAmount.getValue() / 100.0f;
        processor.setFrequencyCut (cut);
        repaint (cutSection);
    };
    addChildComponent (cutAmount);

    bitDepthBox.addItem ("16-bit", 16);
    bitDepthBox.addItem ("24-bit", 24);
    bitDepthBox.addItem ("32-bit float", 32);
    bitDepthBox.setTooltip ("Bit depth of the exported WAV files (16-bit is dithered)");
    bitDepthBox.onChange = [this]
    {
        auto prefs = processor.getExportPreferences();
        const int id = bitDepthBox.getSelectedId();
        prefs.bitDepth = id == 16 ? AudioIO::BitDepth::int16 : id == 32 ? AudioIO::BitDepth::float32 : AudioIO::BitDepth::int24;
        processor.setExportPreferences (prefs);
    };
    addAndMakeVisible (bitDepthBox);

    sampleRateBox.addItem ("Original rate", 1);
    sampleRateBox.addItem ("44.1 kHz", 44100);
    sampleRateBox.addItem ("48 kHz", 48000);
    sampleRateBox.addItem ("96 kHz", 96000);
    sampleRateBox.setTooltip ("Sample rate of the exported files");
    sampleRateBox.onChange = [this]
    {
        auto prefs = processor.getExportPreferences();
        const int id = sampleRateBox.getSelectedId();
        prefs.sampleRate = id <= 1 ? 0.0 : (double) id;
        processor.setExportPreferences (prefs);
    };
    addAndMakeVisible (sampleRateBox);

    exportButton.getProperties().set ("variant", "primary");
    exportButton.onClick = [this] { if (onExport) onExport(); };
    addAndMakeVisible (exportButton);

    refresh();
}

ExportPanel::~ExportPanel() = default;

void ExportPanel::setMode (ExportMode m)
{
    auto prefs = processor.getExportPreferences();

    if (prefs.mode != m)
    {
        prefs.mode = m;
        processor.setExportPreferences (prefs);
    }
}

void ExportPanel::applyPreset (const juce::Array<StemType>& keep)
{
    auto mix = processor.getMix();

    for (int i = 0; i < numStemTypes; ++i)
    {
        auto& s = mix.stems[(size_t) i];
        s.muted = ! keep.contains (fromIndex (i));
        s.solo = false;
    }

    processor.setMix (mix);
}

void ExportPanel::setBusy (bool isBusy, float progress)
{
    if (busy != isBusy || ! juce::approximatelyEqual (busyProgress, progress))
    {
        busy = isBusy;
        busyProgress = progress;
        refresh();
    }
}

juce::String ExportPanel::getSummary() const
{
    const auto source = processor.getSource();
    const auto stems = processor.getStems();
    const auto& prefs = processor.getExportPreferences();

    if (source == nullptr)
        return "Open a song to export.";

    if (prefs.mode == ExportMode::stems)
    {
        if (stems == nullptr)
            return "Separate the song first.";

        int n = 0;
        for (const auto& s : stems->stems)
            if (prefs.stemSelected[(size_t) toIndex (s.type)])
                ++n;

        return n == 0 ? juce::String ("No stems selected.")
                      : juce::String (n) + " WAV file" + (n == 1 ? "" : "s") + ", e.g. \"" + source->file.getFileNameWithoutExtension()
                            + " - " + getStemName (stems->stems.front().type) + ".wav\"";
    }

    return "\"" + source->file.getFileNameWithoutExtension() + " ("
           + MixRenderer::describeMix (stems.get(), processor.getMix(), true) + ").wav\"";
}

void ExportPanel::refresh()
{
    const auto& prefs = processor.getExportPreferences();
    const auto& mix = processor.getMix();
    const auto source = processor.getSource();
    const auto stems = processor.getStems();
    const bool stemsMode = prefs.mode == ExportMode::stems;
    const bool hasStems = stems != nullptr && ! stems->stems.empty();

    modeSelector.setSelectedIndex (stemsMode ? 0 : 1, juce::dontSendNotification);

    for (int i = 0; i < stemChecks.size(); ++i)
    {
        const auto type = displayOrder[(size_t) i];
        const bool present = hasStems && stems->find (type) != nullptr;
        stemChecks[i]->setVisible (stemsMode && present);
        stemChecks[i]->setToggleState (prefs.stemSelected[(size_t) toIndex (type)], juce::dontSendNotification);

        removeChips[i]->setVisible (! stemsMode && present);
        removeChips[i]->setToggleState (mix.stems[(size_t) toIndex (type)].muted, juce::dontSendNotification);
    }

    selectAll.setVisible (stemsMode && hasStems);
    selectNone.setVisible (stemsMode && hasStems);

    for (auto* b : presets)
        b->setVisible (! stemsMode && hasStems);

    // the "No bass"/"No drums" presets only make sense with the matching stems present
    if (presets.size() == 4)
        presets[3]->setEnabled (hasStems && stems->find (StemType::bass) != nullptr);

    for (auto* c : std::initializer_list<juce::Component*> { &cutEnabled, &cutMode, &cutView, &cutAmount })
        c->setVisible (! stemsMode);

    cutEnabled.setToggleState (mix.cut.enabled, juce::dontSendNotification);
    cutMode.setSelectedIndex ((int) mix.cut.mode, juce::dontSendNotification);
    cutView.setSettings (mix.cut);

    if (! cutAmount.isMouseButtonDown())
        cutAmount.setValue (mix.cut.amount * 100.0, juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &cutMode, &cutAmount })
    {
        c->setEnabled (mix.cut.enabled);
        c->setAlpha (mix.cut.enabled ? 1.0f : 0.45f);
    }

    bitDepthBox.setSelectedId ((int) prefs.bitDepth, juce::dontSendNotification);
    sampleRateBox.setSelectedId (prefs.sampleRate > 0 ? (int) prefs.sampleRate : 1, juce::dontSendNotification);

    // Export button
    bool canExport = source != nullptr && ! busy;
    juce::String label;

    if (stemsMode)
    {
        int n = 0;
        if (hasStems)
            for (const auto& s : stems->stems)
                if (prefs.stemSelected[(size_t) toIndex (s.type)])
                    ++n;

        canExport = canExport && n > 0;
        label = n == 1 ? "Export 1 Stem" : "Export " + juce::String (juce::jmax (0, n)) + " Stems";

        if (! hasStems)
            label = "Export Stems";
    }
    else
    {
        label = "Export Mix";
    }

    if (busy)
        label = "Exporting " + juce::String (juce::roundToInt (busyProgress * 100.0f)) + "%";

    exportButton.setButtonText (label);
    exportButton.setEnabled (canExport);

    resized();
    repaint();
}

void ExportPanel::resized()
{
    auto r = getLocalBounds().reduced (20, 18);
    const auto& prefs = processor.getExportPreferences();
    const bool stemsMode = prefs.mode == ExportMode::stems;
    const auto stems = processor.getStems();
    const bool hasStems = stems != nullptr && ! stems->stems.empty();

    r.removeFromTop (headerHeight + 10);   // "EXPORT" title
    modeSelector.setBounds (r.removeFromTop (34));
    r.removeFromTop (14);

    // bottom: export button + summary + format
    auto bottom = r.removeFromBottom (44 + 34 + 30 + headerHeight + 8);
    formatSection = bottom.removeFromTop (headerHeight + 8 + 30);
    auto formatRow = formatSection.withTrimmedTop (headerHeight + 8);
    bitDepthBox.setBounds (formatRow.removeFromLeft ((formatRow.getWidth() - 8) / 2));
    formatRow.removeFromLeft (8);
    sampleRateBox.setBounds (formatRow);
    bottom.removeFromTop (12);
    exportButton.setBounds (bottom.removeFromTop (42));
    summaryArea = bottom.withTrimmedTop (4);

    if (stemsMode)
    {
        stemsSection = r.removeFromTop (headerHeight + 8 + (hasStems ? (int) stems->stems.size() * 30 : 40));
        auto list = stemsSection.withTrimmedTop (headerHeight + 8);
        auto header = stemsSection.withHeight (headerHeight);
        selectNone.setBounds (header.removeFromRight (48).withSizeKeepingCentre (48, 22));
        selectAll.setBounds (header.removeFromRight (36).withSizeKeepingCentre (36, 22));

        for (int i = 0; i < stemChecks.size(); ++i)
            if (stemChecks[i]->isVisible())
                stemChecks[i]->setBounds (list.removeFromTop (30).reduced (2, 3));

        removeSection = cutSection = {};
        return;
    }

    stemsSection = {};

    // Remove instruments
    if (hasStems)
    {
        removeSection = r.removeFromTop (headerHeight + 8 + 2 * 34 + 8 + 2 * 30);
        auto area = removeSection.withTrimmedTop (headerHeight + 8);

        // chips flow in rows of three
        auto row = area.removeFromTop (34);
        int inRow = 0;
        const int chipW = (area.getWidth() - 12) / 3;

        for (auto* chip : removeChips)
        {
            if (! chip->isVisible())
                continue;

            if (inRow == 3)
            {
                row = area.removeFromTop (34);
                inRow = 0;
            }

            chip->setBounds (row.removeFromLeft (chipW).reduced (0, 3));
            row.removeFromLeft (6);
            ++inRow;
        }

        if (removeChips.size() > 0 && inRow <= 3 && area.getHeight() > 2 * 30 + 8 + 34 - 1)
            area.removeFromTop (34);

        area.removeFromTop (8);
        auto presetRows = area;
        const int presetW = (presetRows.getWidth() - 6) / 2;

        for (int i = 0; i < presets.size(); ++i)
        {
            auto rowArea = presetRows.withHeight (30).withY (presetRows.getY() + (i / 2) * 30);
            presets[i]->setBounds (rowArea.withX (rowArea.getX() + (i % 2) * (presetW + 6)).withWidth (presetW).reduced (0, 2));
        }

        r.removeFromTop (sectionGap);
    }
    else
    {
        removeSection = r.removeFromTop (headerHeight + 8 + 44);
        r.removeFromTop (sectionGap - 6);
    }

    // Frequency cut
    cutSection = r.removeFromTop (juce::jmin (r.getHeight(), headerHeight + 8 + 32 + 8 + 116 + 8 + 24));
    auto cut = cutSection;
    cutEnabled.setBounds (cut.removeFromTop (headerHeight + 4).withWidth (cut.getWidth()));
    cut.removeFromTop (6);
    cutMode.setBounds (cut.removeFromTop (30));
    cut.removeFromTop (8);
    cutView.setBounds (cut.removeFromTop (juce::jmax (60, juce::jmin (116, cut.getHeight() - 32))));
    cut.removeFromTop (6);
    cutAmount.setBounds (cut.removeFromTop (22).withTrimmedLeft (52).withTrimmedRight (40));
}

void ExportPanel::paint (juce::Graphics& g)
{
    using namespace Theme::Colours;
    g.setColour (panel);
    g.fillRect (getLocalBounds());
    g.setColour (outlineSoft);
    g.fillRect (0, 0, 1, getHeight());

    auto r = getLocalBounds().reduced (20, 18);
    Theme::drawLabel (g, "Export", r.removeFromTop (headerHeight), textDim, 11.5f);

    const auto& prefs = processor.getExportPreferences();
    const auto stems = processor.getStems();
    const bool hasStems = stems != nullptr && ! stems->stems.empty();

    if (! stemsSection.isEmpty())
    {
        Theme::drawLabel (g, "Stems to export", stemsSection.withHeight (headerHeight), textFaint);

        if (! hasStems)
        {
            g.setColour (textFaint);
            g.setFont (Theme::font (12.5f));
            g.drawFittedText ("Separate the song first. Each stem is saved as its own WAV file.",
                              stemsSection.withTrimmedTop (headerHeight + 8), juce::Justification::topLeft, 3);
        }
    }

    if (! removeSection.isEmpty())
    {
        Theme::drawLabel (g, "Remove instruments", removeSection.withHeight (headerHeight), textFaint);

        if (! hasStems)
        {
            g.setColour (textFaint);
            g.setFont (Theme::font (12.5f));
            g.drawFittedText ("Separate the song to remove vocals, drums, bass or other instruments.",
                              removeSection.withTrimmedTop (headerHeight + 8), juce::Justification::topLeft, 3);
        }
    }

    if (! cutSection.isEmpty() && prefs.mode == ExportMode::mix)
    {
        // amount label + value
        const auto amountRow = cutAmount.getBounds();
        g.setColour (processor.getMix().cut.enabled ? textDim : textFaint);
        g.setFont (Theme::font (11.5f));
        g.drawText ("Depth", amountRow.withX (cutSection.getX()).withWidth (48), juce::Justification::centredLeft);
        g.drawText (juce::String (juce::roundToInt (processor.getMix().cut.amount * 100.0f)) + "%",
                    amountRow.withX (amountRow.getRight() + 4).withWidth (36), juce::Justification::centredRight);
    }

    Theme::drawLabel (g, "Format", formatSection.withHeight (headerHeight), textFaint);

    g.setColour (textFaint);
    g.setFont (Theme::font (11.5f));
    g.drawFittedText (getSummary(), summaryArea, juce::Justification::centredTop, 2);
}

} // namespace stemsplitter
