#include "ModelOverlay.h"

namespace stemsplitter
{

ModelOverlay::ModelOverlay (StemSplitterProcessor& p) : processor (p)
{
    downloadButton.getProperties().set ("variant", "primary");
    downloadButton.onClick = [this]
    {
        error.clear();
        processor.downloadModel (engine);
        refresh();
    };

    locateButton.getProperties().set ("variant", "ghost");
    locateButton.setTooltip ("Install model files you downloaded yourself (ggml-model-*.bin)");
    locateButton.onClick = [this] { if (onLocateFiles) onLocateFiles(); };

    liteButton.getProperties().set ("variant", "ghost");
    liteButton.setTooltip (getEngineDescription (EngineType::lite));
    liteButton.onClick = [this]
    {
        processor.setEngine (EngineType::lite);
        hide();

        if (shouldSeparateWhenReady)
            processor.startSeparation();
    };

    closeButton.getProperties().set ("variant", "ghost");
    closeButton.onClick = [this]
    {
        if (processor.getDownloader().isDownloading())
            processor.getDownloader().cancel();

        hide();
    };

    for (auto* b : { &downloadButton, &locateButton, &liteButton, &closeButton })
        addAndMakeVisible (*b);

    addChildComponent (progressBar);
    setInterceptsMouseClicks (true, true);
}

void ModelOverlay::show (EngineType e)
{
    engine = e;
    error.clear();
    setVisible (true);
    toFront (true);
    refresh();
}

void ModelOverlay::hide()
{
    shouldSeparateWhenReady = false;
    setVisible (false);
}

void ModelOverlay::refresh()
{
    if (! isVisible())
        return;

    const auto& dl = processor.getDownloader();
    const bool downloading = dl.isDownloading() && dl.getEngine() == engine;

    if (ModelManager::isInstalled (engine) && ! downloading)
    {
        const bool separate = shouldSeparateWhenReady;
        hide();

        if (onReady)
            onReady();

        if (separate)
        {
            processor.setEngine (engine);
            processor.startSeparation();
        }

        return;
    }

    progress = dl.getProgress();
    error = (! downloading && dl.getEngine() == engine) ? dl.getLastError() : juce::String();
    progressBar.setVisible (downloading);
    downloadButton.setVisible (! downloading);
    locateButton.setVisible (! downloading);
    liteButton.setVisible (! downloading);
    closeButton.setButtonText (downloading ? "Cancel download" : "Not now");

    const auto mb = ModelManager::getDownloadSize (engine) / (1024 * 1024);
    downloadButton.setButtonText (juce::String::fromUTF8 ("Download  ·  ") + juce::String (mb) + " MB");

    resized();
    repaint();
}

juce::Rectangle<int> ModelOverlay::getCardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (460, getWidth() - 40), error.isNotEmpty() ? 300 : 270);
}

void ModelOverlay::resized()
{
    auto card = getCardBounds().reduced (28, 24);
    auto buttons = card.removeFromBottom (36);

    if (progressBar.isVisible())
    {
        progressBar.setBounds (card.removeFromBottom (8).withTrimmedBottom (0));
        closeButton.setBounds (buttons.removeFromRight (150));
        return;
    }

    downloadButton.setBounds (buttons.removeFromLeft (190));
    closeButton.setBounds (buttons.removeFromRight (90));

    auto secondRow = card.removeFromBottom (44).withTrimmedBottom (10);
    locateButton.setBounds (secondRow.removeFromLeft (150));
    liteButton.setBounds (secondRow.removeFromLeft (150));
}

void ModelOverlay::paint (juce::Graphics& g)
{
    using namespace Theme::Colours;
    g.fillAll (juce::Colours::black.withAlpha (0.6f));

    const auto card = getCardBounds();
    g.setColour (panel);
    g.fillRoundedRectangle (card.toFloat(), 14.0f);
    g.setColour (outline);
    g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 14.0f, 1.0f);

    auto r = card.reduced (28, 24);
    const auto& dl = processor.getDownloader();
    const bool downloading = dl.isDownloading() && dl.getEngine() == engine;

    g.setColour (Theme::Colours::text);
    g.setFont (Theme::font (19.0f, true));
    g.drawText (downloading ? "Downloading the " + getEngineName (engine) + " model"
                            : "Get the " + getEngineName (engine) + " model",
                r.removeFromTop (30), juce::Justification::centredLeft);
    r.removeFromTop (8);

    juce::String body;

    if (downloading)
    {
        body << juce::String (juce::roundToInt (dl.getProgress() * 100.0f)) << "% of "
             << juce::String (ModelManager::getDownloadSize (engine) / (1024 * 1024)) << " MB. "
             << "You only need to do this once. It's saved in:\n"
             << ModelManager::getUserModelDirectory().getFullPathName();
    }
    else
    {
        body << getEngineDescription (engine) << "\n\n"
             << "Separation runs entirely on your Mac using a neural network. It needs a one-time download of the "
             << "model (" << juce::String (ModelManager::getDownloadSize (engine) / (1024 * 1024)) << " MB).";
    }

    g.setColour (textDim);
    g.setFont (Theme::font (13.5f));
    g.drawFittedText (body, r.removeFromTop (downloading ? 60 : 84), juce::Justification::topLeft, 7);

    if (error.isNotEmpty())
    {
        g.setColour (danger);
        g.setFont (Theme::font (12.5f));
        g.drawFittedText (error, r.removeFromTop (34), juce::Justification::topLeft, 2);
    }
}

void ModelOverlay::mouseUp (const juce::MouseEvent& e)
{
    // click on the backdrop closes (unless downloading)
    if (! getCardBounds().contains (e.getPosition()) && ! processor.getDownloader().isDownloading())
        hide();
}

} // namespace stemsplitter
