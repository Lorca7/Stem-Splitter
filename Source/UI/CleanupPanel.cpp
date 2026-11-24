#include "CleanupPanel.h"

namespace stemsplitter
{

namespace
{
    constexpr int rowH = 22;
    constexpr int labelW = 82;
    constexpr int valueW = 50;

    void styleSlider (juce::Slider& s, double min, double max, double step, double defaultValue, const juce::String& tip)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (min, max, step);
        s.setDoubleClickReturnValue (true, defaultValue);
        s.setTooltip (tip + " (double-click to reset)");
    }
}

class CleanupPanel::Content final : public juce::Component
{
public:
    explicit Content (StemSplitterProcessor& p) : processor (p)
    {
        master.setTooltip ("Switch the clean-up on or off to compare (B)");
        master.onClick = [this] { update ([this] (CleanupSettings& s) { s.enabled = master.getToggleState(); }); };
        addAndMakeVisible (master);

        spectrum.onNotchesChanged = [this] (std::vector<float> n)
        {
            update ([&n] (CleanupSettings& s) { s.notchesHz = std::move (n); s.enabled = true; });
        };
        addAndMakeVisible (spectrum);

        findTones.setTooltip ("Find steady tones (fan whine, buzz, whistle) and remove them");
        findTones.onClick = [this]
        {
            const auto a = processor.getCleanupEngine().getAnalysis();
            if (a == nullptr)
                return;

            update ([&a] (CleanupSettings& s)
            {
                for (auto hz : a->suggestedTonesHz)
                {
                    const bool exists = std::any_of (s.notchesHz.begin(), s.notchesHz.end(),
                                                     [hz] (float n) { return std::abs (n - hz) < hz * 0.02f; });
                    if (! exists && (int) s.notchesHz.size() < CleanupSettings::maxNotches)
                        s.notchesHz.push_back (hz);
                }

                std::sort (s.notchesHz.begin(), s.notchesHz.end());
                s.enabled = true;
            });
        };

        clearNotches.setTooltip ("Remove all tone notches");
        clearNotches.onClick = [this] { update ([] (CleanupSettings& s) { s.notchesHz.clear(); }); };

        for (auto* b : { &findTones, &clearNotches })
        {
            b->getProperties().set ("variant", "ghost");
            addAndMakeVisible (*b);
        }

        styleSlider (notchWidth, 0.0, 1.0, 0.01, 0.5, "How wide each notch is: narrow keeps more of the music, wide catches drifting tones");
        notchWidth.onValueChange = [this] { update ([this] (CleanupSettings& s) { s.notchWidth = (float) notchWidth.getValue(); }); };
        addAndMakeVisible (notchWidth);

        // Background noise
        denoise.setTooltip ("Reduce steady background noise: fans, air conditioning, hiss, room tone");
        denoise.onClick = [this] { update ([this] (CleanupSettings& s) { s.denoise = denoise.getToggleState(); if (s.denoise) s.enabled = true; }); };
        addAndMakeVisible (denoise);

        styleSlider (reduction, 3.0, 40.0, 0.5, 15.0, "How much the noise is turned down");
        reduction.onValueChange = [this] { update ([this] (CleanupSettings& s) { s.reductionDb = (float) reduction.getValue(); }); };
        addAndMakeVisible (reduction);

        styleSlider (sensitivity, 0.0, 1.0, 0.01, 0.5, "Higher removes more noise but can thin out quiet detail");
        sensitivity.onValueChange = [this] { update ([this] (CleanupSettings& s) { s.sensitivity = (float) sensitivity.getValue(); }); };
        addAndMakeVisible (sensitivity);

        learn.setTooltip ("Put the playhead on a spot with only noise (e.g. before the music starts), then click: "
                          "the next 2 seconds become the noise profile");
        learn.onClick = [this] { processor.learnNoiseProfileAtPlayhead (2.0); };
        automatic.setTooltip ("Learn the noise automatically from the quietest parts of the recording");
        automatic.onClick = [this] { processor.useAutomaticNoiseProfile(); };

        for (auto* b : { &learn, &automatic })
            addAndMakeVisible (*b);

        // Hum
        dehum.setTooltip ("Remove electrical mains hum (50 or 60 Hz) and its harmonics");
        dehum.onClick = [this] { update ([this] (CleanupSettings& s) { s.dehum = dehum.getToggleState(); if (s.dehum) s.enabled = true; }); };
        addAndMakeVisible (dehum);

        humMode.setOptions ({ "Auto", "50 Hz", "60 Hz" },
                            { "Detect the mains frequency from the recording", "Europe, Asia, Africa, Australia", "North America, parts of South America and Asia" });
        humMode.onChange = [this] (int i) { update ([i] (CleanupSettings& s) { s.humHz = i == 1 ? 50 : i == 2 ? 60 : 0; s.dehum = s.enabled = true; }); };
        addAndMakeVisible (humMode);

        styleSlider (harmonics, 1.0, 20.0, 1.0, 8.0, "How many harmonics of the hum to remove (buzz has many)");
        harmonics.onValueChange = [this] { update ([this] (CleanupSettings& s) { s.humHarmonics = (int) harmonics.getValue(); }); };
        addAndMakeVisible (harmonics);

        // Rumble
        rumble.setTooltip ("Remove low-frequency rumble: handling noise, traffic, wind, stage vibration");
        rumble.onClick = [this] { update ([this] (CleanupSettings& s) { s.rumble = rumble.getToggleState(); if (s.rumble) s.enabled = true; }); };
        addAndMakeVisible (rumble);

        styleSlider (rumbleHz, 20.0, 200.0, 1.0, 50.0, "Everything below this frequency is removed");
        rumbleHz.setSkewFactorFromMidPoint (70.0);
        rumbleHz.onValueChange = [this] { update ([this] (CleanupSettings& s) { s.rumbleHz = (float) rumbleHz.getValue(); }); };
        addAndMakeVisible (rumbleHz);
    }

    int getIdealHeight() const { return 512; }

    void refresh()
    {
        const auto& s = processor.getCleanupSettings();
        const auto& engine = processor.getCleanupEngine();
        const auto analysis = engine.getAnalysis();
        const bool hasSource = processor.getSource() != nullptr;
        humHz = analysis != nullptr ? NoiseCleanup::resolveHumHz (s, *analysis) : 0.0f;

        master.setToggleState (s.enabled, juce::dontSendNotification);
        denoise.setToggleState (s.denoise, juce::dontSendNotification);
        dehum.setToggleState (s.dehum, juce::dontSendNotification);
        rumble.setToggleState (s.rumble, juce::dontSendNotification);
        humMode.setSelectedIndex (s.humHz == 50 ? 1 : s.humHz == 60 ? 2 : 0, juce::dontSendNotification);

        const auto detected = analysis != nullptr && analysis->detectedHumHz > 0.0f
                                  ? "Auto (" + juce::String ((int) analysis->detectedHumHz) + ")"
                                  : juce::String ("Auto");
        humMode.setOptions ({ detected, "50 Hz", "60 Hz" },
                            { analysis != nullptr && analysis->detectedHumHz <= 0.0f ? "No hum detected; 60 Hz is used"
                                                                                     : "Use the detected mains frequency",
                              "Europe, Asia, Africa, Australia", "North America, parts of South America and Asia" });
        humMode.setSelectedIndex (s.humHz == 50 ? 1 : s.humHz == 60 ? 2 : 0, juce::dontSendNotification);

        auto setIfIdle = [] (juce::Slider& sl, double v) { if (! sl.isMouseButtonDown()) sl.setValue (v, juce::dontSendNotification); };
        setIfIdle (reduction, s.reductionDb);
        setIfIdle (sensitivity, s.sensitivity);
        setIfIdle (harmonics, s.humHarmonics);
        setIfIdle (rumbleHz, s.rumbleHz);
        setIfIdle (notchWidth, s.notchWidth);

        spectrum.setData (analysis, s, s.dehum ? humHz : 0.0f);

        for (auto* c : std::initializer_list<juce::Component*> { &master, &spectrum, &findTones, &denoise, &dehum, &rumble, &learn })
            c->setEnabled (hasSource);

        findTones.setEnabled (hasSource && analysis != nullptr && ! analysis->suggestedTonesHz.empty());
        findTones.setButtonText (analysis != nullptr && ! analysis->suggestedTonesHz.empty()
                                     ? "Find tones (" + juce::String ((int) analysis->suggestedTonesHz.size()) + ")"
                                     : juce::String ("Find tones"));
        clearNotches.setEnabled (! s.notchesHz.empty());
        automatic.setEnabled (engine.hasCustomProfile());

        // Sections look subdued while the clean-up is switched off, but stay clickable (switching one on
        // switches the clean-up on).
        const float sectionAlpha = s.enabled ? 1.0f : 0.6f;
        for (auto* c : std::initializer_list<juce::Component*> { &spectrum, &denoise, &dehum, &rumble, &learn, &findTones, &clearNotches })
            c->setAlpha (sectionAlpha);

        auto dim = [sectionAlpha] (juce::Component& c, bool active) { c.setEnabled (active); c.setAlpha (active ? sectionAlpha : 0.45f); };
        dim (reduction, hasSource && s.denoise);
        dim (sensitivity, hasSource && s.denoise);
        dim (humMode, hasSource && s.dehum);
        dim (harmonics, hasSource && s.dehum);
        dim (rumbleHz, hasSource && s.rumble);
        dim (notchWidth, hasSource && ! s.notchesHz.empty());

        // Status line
        if (! hasSource)                 status = "Open a song to clean it up";
        else if (engine.isAnalysing())   status = "Analysing...";
        else if (! s.enabled)            status = "Off (press B to compare)";
        else if (! s.hasAnyEffect())     status = "Choose what to remove";
        else if (engine.isRendering())   status = "Applying " + juce::String (juce::roundToInt (engine.getProgress() * 100.0f)) + "%";
        else                             status = "Applied to the original and every stem";

        profileText = engine.hasCustomProfile() ? "Noise profile: learned from playhead" : "Noise profile: automatic";
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (28);
        master.setBounds (header.removeFromLeft (120));
        r.removeFromTop (22);

        spectrum.setBounds (r.removeFromTop (136));
        r.removeFromTop (6);

        auto notchRow = r.removeFromTop (26);
        findTones.setBounds (notchRow.removeFromLeft (112));
        clearNotches.setBounds (notchRow.removeFromLeft (56));
        notchWidthLabel = notchRow.removeFromLeft (42);
        notchWidth.setBounds (notchRow);
        r.removeFromTop (16);

        denoise.setBounds (r.removeFromTop (rowH).withWidth (180));
        r.removeFromTop (4);
        layoutSliderRow (r.removeFromTop (rowH), reduction, reductionLabel, reductionValue);
        layoutSliderRow (r.removeFromTop (rowH), sensitivity, sensitivityLabel, sensitivityValue);
        r.removeFromTop (2);
        profileArea = r.removeFromTop (16);
        auto profileRow = r.removeFromTop (28).reduced (0, 2);
        learn.setBounds (profileRow.removeFromLeft (150));
        profileRow.removeFromLeft (6);
        automatic.setBounds (profileRow.removeFromLeft (90));
        r.removeFromTop (14);

        dehum.setBounds (r.removeFromTop (rowH).withWidth (180));
        r.removeFromTop (4);
        humMode.setBounds (r.removeFromTop (28));
        r.removeFromTop (4);
        layoutSliderRow (r.removeFromTop (rowH), harmonics, harmonicsLabel, harmonicsValue);
        r.removeFromTop (14);

        rumble.setBounds (r.removeFromTop (rowH).withWidth (180));
        r.removeFromTop (4);
        layoutSliderRow (r.removeFromTop (rowH), rumbleHz, rumbleLabel, rumbleValue);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Theme::Colours;
        const auto& s = processor.getCleanupSettings();

        // status, right-aligned in the header row
        const bool busy = processor.getCleanupEngine().isRendering() || processor.getCleanupEngine().isAnalysing();
        g.setColour (busy ? warning : (s.enabled && s.hasAnyEffect() ? success : textFaint));
        g.setFont (Theme::font (11.5f));
        g.drawFittedText (status, getLocalBounds().removeFromTop (28).withTrimmedLeft (124), juce::Justification::centredRight, 1);

        g.setColour (textFaint);
        g.setFont (Theme::font (11.5f));
        g.drawText ("Click a peak in the spectrum to remove that frequency", getLocalBounds().withY (30).withHeight (16),
                    juce::Justification::centredLeft, true);

        g.setFont (Theme::font (11.0f));
        g.setColour (textDim);
        g.drawText ("Width", notchWidthLabel, juce::Justification::centredRight);

        auto drawValue = [&] (juce::Rectangle<int> labelArea, juce::Rectangle<int> valueArea, const juce::String& label,
                              const juce::String& value, bool active)
        {
            g.setColour (active ? textDim : textFaint);
            g.setFont (Theme::font (11.5f));
            g.drawText (label, labelArea, juce::Justification::centredLeft);
            g.drawText (value, valueArea, juce::Justification::centredRight);
        };

        drawValue (reductionLabel, reductionValue, "Reduce by", juce::String (s.reductionDb, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " dB", s.denoise);
        drawValue (sensitivityLabel, sensitivityValue, "Sensitivity", juce::String (juce::roundToInt (s.sensitivity * 100.0f)) + "%", s.denoise);
        drawValue (harmonicsLabel, harmonicsValue, "Harmonics", juce::String (s.humHarmonics), s.dehum);
        drawValue (rumbleLabel, rumbleValue, "Below", juce::String (juce::roundToInt (s.rumbleHz)) + " Hz", s.rumble);

        g.setColour (s.denoise ? textFaint : textFaint.withAlpha (0.6f));
        g.setFont (Theme::font (11.0f));
        g.drawText (profileText, profileArea, juce::Justification::centredLeft, true);

        // hints next to the section toggles
        g.setColour (textFaint);
        g.drawText ("fans, AC, hiss", denoise.getBounds().withX (denoise.getRight()).withWidth (getWidth() - denoise.getRight()),
                    juce::Justification::centredRight);
        g.drawText ("50/60 Hz buzz", dehum.getBounds().withX (dehum.getRight()).withWidth (getWidth() - dehum.getRight()),
                    juce::Justification::centredRight);
        g.drawText ("traffic, handling", rumble.getBounds().withX (rumble.getRight()).withWidth (getWidth() - rumble.getRight()),
                    juce::Justification::centredRight);
    }

private:
    void update (const std::function<void (CleanupSettings&)>& change)
    {
        auto s = processor.getCleanupSettings();
        change (s);
        processor.setCleanupSettings (s);
    }

    static void layoutSliderRow (juce::Rectangle<int> row, juce::Slider& slider, juce::Rectangle<int>& label, juce::Rectangle<int>& value)
    {
        label = row.removeFromLeft (labelW);
        value = row.removeFromRight (valueW);
        slider.setBounds (row.reduced (4, 0));
    }

    StemSplitterProcessor& processor;
    juce::ToggleButton master { "Clean up" }, denoise { "Background noise" }, dehum { "Mains hum" }, rumble { "Rumble" };
    SpectrumView spectrum;
    juce::TextButton findTones { "Find tones" }, clearNotches { "Clear" }, learn { "Learn at playhead" }, automatic { "Automatic" };
    juce::Slider notchWidth, reduction, sensitivity, harmonics, rumbleHz;
    SegmentedControl humMode;

    juce::Rectangle<int> notchWidthLabel, reductionLabel, reductionValue, sensitivityLabel, sensitivityValue,
                         harmonicsLabel, harmonicsValue, rumbleLabel, rumbleValue, profileArea;
    juce::String status, profileText;
    float humHz = 0.0f;
};

//==============================================================================
CleanupPanel::CleanupPanel (StemSplitterProcessor& p) : processor (p)
{
    content = std::make_unique<Content> (p);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);
    refresh();
}

CleanupPanel::~CleanupPanel() = default;

void CleanupPanel::refresh()
{
    content->refresh();
}

void CleanupPanel::resized()
{
    viewport.setBounds (getLocalBounds().reduced (20, 0).withTrimmedBottom (12));
    const bool scroll = viewport.getHeight() < content->getIdealHeight();
    content->setSize (viewport.getWidth() - (scroll ? 10 : 0), content->getIdealHeight());
}

void CleanupPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::Colours::panel);
}

} // namespace stemsplitter
