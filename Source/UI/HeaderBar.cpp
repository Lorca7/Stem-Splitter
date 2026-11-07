#include "HeaderBar.h"
#include "Icons.h"

namespace stemsplitter
{

/** Model status pill: "Model ready", "Get model · 81 MB" or "Downloading 42%" (click to cancel). */
class HeaderBar::StatusChip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class State { ready, missing, downloading, notNeeded };

    void set (State s, const juce::String& t, float p)
    {
        if (s != state || t != text || ! juce::approximatelyEqual (p, progress))
        {
            state = s;
            text = t;
            progress = p;
            setMouseCursor (state == State::missing || state == State::downloading ? juce::MouseCursor::PointingHandCursor
                                                                                   : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    std::function<void()> onClick;

    int getIdealWidth() const
    {
        return juce::GlyphArrangement::getStringWidthInt (Theme::font (12.5f, true), text) + 44;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Theme::Colours;
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool hover = isMouseOver (true);

        switch (state)
        {
            case State::ready:
            case State::notNeeded:
                g.setColour (success.withAlpha (0.1f));
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                g.setColour (success);
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + 15.0f, r.getCentreY() }));
                g.setColour (Theme::Colours::text.withAlpha (0.85f));
                break;

            case State::missing:
                g.setColour (hover ? accentBright : accent);
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                {
                    const auto icon = Icons::download();
                    g.setColour (juce::Colours::white);
                    g.fillPath (icon, icon.getTransformToScaleToFit (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ r.getX() + 16.0f, r.getCentreY() }), true));
                }
                g.setColour (juce::Colours::white);
                break;

            case State::downloading:
                g.setColour (raised);
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                g.setColour (accent.withAlpha (0.35f));
                g.fillRoundedRectangle (r.withWidth (juce::jmax (r.getHeight(), r.getWidth() * progress)), r.getHeight() * 0.5f);
                {
                    const auto icon = Icons::close();
                    g.setColour (hover ? Theme::Colours::text : textDim);
                    g.fillPath (icon, icon.getTransformToScaleToFit (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ r.getX() + 16.0f, r.getCentreY() }), true));
                }
                g.setColour (Theme::Colours::text);
                break;
        }

        g.setFont (Theme::font (12.5f, true));
        g.drawText (text, r.withTrimmedLeft (28.0f).withTrimmedRight (12.0f), juce::Justification::centredLeft);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && (state == State::missing || state == State::downloading) && onClick)
            onClick();
    }

    State getState() const noexcept { return state; }

private:
    State state = State::ready;
    juce::String text;
    float progress = 0.0f;
};

//==============================================================================
HeaderBar::HeaderBar (StemSplitterProcessor& p) : processor (p)
{
    juce::StringArray names, tips;

    for (int i = 0; i < numEngineTypes; ++i)
    {
        names.add (getEngineName ((EngineType) i));
        tips.add (getEngineDescription ((EngineType) i));
    }

    engineSelector.setOptions (names, tips);
    engineSelector.onChange = [this] (int i) { processor.setEngine ((EngineType) i); };
    addAndMakeVisible (engineSelector);

    status = std::make_unique<StatusChip>();
    status->onClick = [this]
    {
        if (status->getState() == StatusChip::State::downloading)
        {
            if (onCancelDownload) onCancelDownload();
        }
        else if (onDownloadModel)
        {
            onDownloadModel();
        }
    };
    addAndMakeVisible (*status);

    openButton = std::make_unique<IconButton> ("Open", Icons::folder());
    openButton->setTooltip ("Open an audio file (Cmd+O)");
    openButton->onClick = [this] { if (onOpenFile) onOpenFile(); };
    addAndMakeVisible (*openButton);

    settingsButton = std::make_unique<IconButton> ("Audio settings", Icons::settings());
    settingsButton->setTooltip ("Audio output settings");
    settingsButton->onClick = [this] { if (onSettings) onSettings(); };
    addChildComponent (*settingsButton);

    refresh();
}

HeaderBar::~HeaderBar() = default;

void HeaderBar::refresh()
{
    const auto engine = processor.getEngine();
    engineSelector.setSelectedIndex ((int) engine, juce::dontSendNotification);
    engineSelector.setEnabled (! processor.isSeparating());

    for (int i = 0; i < numEngineTypes; ++i)
    {
        const auto e = (EngineType) i;
        engineSelector.setOptionBadge (i, ModelManager::isInstalled (e) ? juce::String() : juce::String ("GET"));
    }

    const auto& dl = processor.getDownloader();

    if (dl.isDownloading())
    {
        status->set (StatusChip::State::downloading,
                     "Downloading " + juce::String (juce::roundToInt (dl.getProgress() * 100.0f)) + "%", dl.getProgress());
        status->setTooltip ("Click to cancel the download");
    }
    else if (engine == EngineType::lite)
    {
        status->set (StatusChip::State::notNeeded, "No download needed", 0.0f);
        status->setTooltip ("The Lite engine uses signal processing and needs no model");
    }
    else if (ModelManager::isInstalled (engine))
    {
        status->set (StatusChip::State::ready, "Model ready", 0.0f);
        status->setTooltip (getEngineDescription (engine));
    }
    else
    {
        const auto mb = ModelManager::getDownloadSize (engine) / (1024 * 1024);
        status->set (StatusChip::State::missing, juce::String::fromUTF8 ("Get model  ·  ") + juce::String (mb) + " MB", 0.0f);
        status->setTooltip ("Download the " + getEngineName (engine) + " neural network (one time)");
    }

    settingsButton->setVisible (processor.showAudioSettings != nullptr);
    resized();
}

void HeaderBar::resized()
{
    auto r = getLocalBounds().reduced (Theme::gutter, 12);
    r.removeFromLeft (170);   // logo

    if (settingsButton->isVisible())
    {
        settingsButton->setBounds (r.removeFromRight (34).withSizeKeepingCentre (34, 34));
        r.removeFromRight (4);
    }

    openButton->setBounds (r.removeFromRight (34).withSizeKeepingCentre (34, 34));
    r.removeFromRight (16);

    const int chipW = juce::jmin (210, status->getIdealWidth());
    status->setBounds (r.removeFromRight (chipW).withSizeKeepingCentre (chipW, 30));
    r.removeFromRight (12);

    const int selW = juce::jmin (r.getWidth(), engineSelector.getIdealWidth() + 40);
    engineSelector.setBounds (r.removeFromRight (selW).withSizeKeepingCentre (selW, 36));
}

void HeaderBar::paint (juce::Graphics& g)
{
    using namespace Theme::Colours;
    g.setColour (background);
    g.fillRect (getLocalBounds());
    g.setColour (outlineSoft);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    auto r = getLocalBounds().reduced (Theme::gutter, 0);
    auto logoArea = r.removeFromLeft (30).toFloat().withSizeKeepingCentre (26.0f, 26.0f);

    // logo: four bars in stem colours
    const std::array<StemType, 4> order { StemType::vocals, StemType::drums, StemType::bass, StemType::other };
    const std::array<float, 4> heights { 0.45f, 1.0f, 0.7f, 0.35f };

    for (size_t i = 0; i < 4; ++i)
    {
        const float w = 4.0f, gap = 3.0f;
        const float h = logoArea.getHeight() * heights[i];
        g.setColour (Theme::stemColour (order[i]));
        g.fillRoundedRectangle (logoArea.getX() + (float) i * (w + gap), logoArea.getCentreY() - h * 0.5f, w, h, 2.0f);
    }

    r.removeFromLeft (8);
    g.setColour (Theme::Colours::text);
    g.setFont (Theme::font (16.0f, true));
    g.drawText ("Stem Splitter", r.removeFromLeft (130), juce::Justification::centredLeft);
}

} // namespace stemsplitter
