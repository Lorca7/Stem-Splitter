#include "TransportBar.h"
#include "Icons.h"

namespace stemsplitter
{

TransportBar::TransportBar (StemSplitterProcessor& p) : processor (p)
{
    playButton = std::make_unique<IconButton> ("Play", Icons::play());
    playButton->setFilledBackground (true);
    playButton->setIconScale (0.42f);
    playButton->setTooltip ("Play / pause (Space)");
    playButton->onClick = [this] { processor.togglePlayback(); update(); };
    addAndMakeVisible (*playButton);

    startButton = std::make_unique<IconButton> ("Back to start", Icons::stop());
    startButton->setTooltip ("Stop and go back to the start (Return)");
    startButton->onClick = [this] { processor.setPlaying (false); processor.seekToStart(); update(); };
    addAndMakeVisible (*startButton);

    loopButton = std::make_unique<IconButton> ("Loop", Icons::loop());
    loopButton->setClickingTogglesState (true);
    loopButton->setTooltip ("Loop playback (L)");
    loopButton->onClick = [this] { processor.setLooping (loopButton->getToggleState()); };
    addAndMakeVisible (*loopButton);

    update();
}

TransportBar::~TransportBar() = default;

void TransportBar::update()
{
    const auto source = processor.getSource();
    const bool hasSource = source != nullptr;
    const bool isPlaying = processor.isPlaying();

    for (auto* b : { playButton.get(), startButton.get(), loopButton.get() })
        b->setEnabled (hasSource);

    if (isPlaying != wasPlaying)
    {
        wasPlaying = isPlaying;
        playButton->setIcon (isPlaying ? Icons::pause() : Icons::play());
        playButton->setTooltip (isPlaying ? "Pause (Space)" : "Play (Space)");
    }

    loopButton->setToggleState (processor.isLooping(), juce::dontSendNotification);

    juce::String t;

    if (hasSource)
    {
        const double pos = (double) processor.getPlayPosition() / source->sampleRate;
        t = Theme::formatTime (pos, true) + "  /  " + Theme::formatTime (source->getLengthSeconds(), true);
    }

    if (t != timeText)
    {
        timeText = t;
        repaint();
    }
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (Theme::gutter, 0);
    playButton->setBounds (r.removeFromLeft (40).withSizeKeepingCentre (40, 40));
    r.removeFromLeft (8);
    startButton->setBounds (r.removeFromLeft (34).withSizeKeepingCentre (34, 34));
    r.removeFromLeft (2);
    loopButton->setBounds (r.removeFromLeft (34).withSizeKeepingCentre (34, 34));
}

void TransportBar::paint (juce::Graphics& g)
{
    using namespace Theme::Colours;
    g.setColour (background);
    g.fillRect (getLocalBounds());
    g.setColour (outlineSoft);
    g.fillRect (0, 0, getWidth(), 1);

    auto r = getLocalBounds().reduced (Theme::gutter, 0);
    r.removeFromLeft (40 + 8 + 34 + 2 + 34 + 18);

    g.setColour (timeText.isEmpty() ? textFaint : Theme::Colours::text);
    g.setFont (Theme::monoFont (13.5f));
    g.drawText (timeText.isEmpty() ? juce::String ("0:00.0  /  0:00.0") : timeText, r.removeFromLeft (200), juce::Justification::centredLeft);

    g.setColour (textFaint);
    g.setFont (Theme::font (12.0f));
    g.drawText (juce::String::fromUTF8 ("Space  play   ·   click a waveform to seek   ·   drag a stem's handle into your DAW"),
                r, juce::Justification::centredRight, true);
}

} // namespace stemsplitter
