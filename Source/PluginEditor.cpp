#include "PluginEditor.h"
#include "UI/Icons.h"

namespace stemsplitter
{

namespace
{
    constexpr int headerHeight = 64;
    constexpr int transportHeight = 58;
    constexpr int exportPanelWidth = 320;
    constexpr int fileRowHeight = 64;

    const std::array<StemType, numStemTypes> displayOrder { StemType::vocals, StemType::drums, StemType::bass,
                                                            StemType::guitar, StemType::piano, StemType::other };
}

//==============================================================================
/** A transient message at the bottom of the window, optionally with "Show in Finder". */
class StemSplitterEditor::Toast final : public juce::Component, private juce::Timer
{
public:
    Toast()
    {
        revealButton.getProperties().set ("variant", "ghost");
        revealButton.onClick = [this]
        {
            if (target.exists())
                target.revealToUser();

            dismiss();
        };
        addChildComponent (revealButton);
        setInterceptsMouseClicks (true, true);
    }

    void show (const juce::String& message, const juce::File& revealTarget)
    {
        text = message;
        target = revealTarget;
        isError = message.containsIgnoreCase ("fail") || message.containsIgnoreCase ("couldn't")
               || message.containsIgnoreCase ("isn't") || message.containsIgnoreCase ("error")
               || message.containsIgnoreCase ("not enough");
        revealButton.setVisible (target != juce::File());
       #if JUCE_MAC
        revealButton.setButtonText ("Show in Finder");
       #else
        revealButton.setButtonText ("Show");
       #endif

        if (auto* parent = getParentComponent())
        {
            const int w = juce::jmin (parent->getWidth() - 40, getIdealWidth());
            setBounds (getBounds().withSizeKeepingCentre (w, 44));
        }

        setVisible (true);
        toFront (false);
        repaint();
        startTimer (target != juce::File() ? 8000 : 4500);
    }

    int getIdealWidth() const
    {
        return juce::GlyphArrangement::getStringWidthInt (Theme::font (13.0f), text) + 56 + (target != juce::File() ? 130 : 0);
    }

    void dismiss()
    {
        stopTimer();
        setVisible (false);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Theme::Colours;
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (raisedHover);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (isError ? danger.withAlpha (0.6f) : outline);
        g.drawRoundedRectangle (r, 10.0f, 1.0f);

        auto content = getLocalBounds().reduced (16, 0);
        g.setColour (isError ? danger : success);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) content.getX() + 4.0f, (float) content.getCentreY() }));
        content.removeFromLeft (18);

        if (revealButton.isVisible())
            content.removeFromRight (124);

        g.setColour (Theme::Colours::text);
        g.setFont (Theme::font (13.0f));
        g.drawFittedText (text, content, juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        revealButton.setBounds (getLocalBounds().removeFromRight (132).reduced (6, 7));
    }

    void mouseUp (const juce::MouseEvent&) override { dismiss(); }

private:
    void timerCallback() override { dismiss(); }

    juce::String text;
    juce::File target;
    bool isError = false;
    juce::TextButton revealButton;
};

//==============================================================================
/** Writes exports off the message thread so the UI stays responsive. */
class StemSplitterEditor::ExportThread final : public juce::Thread
{
public:
    using Job = std::function<juce::Result (juce::Array<juce::File>&, std::function<bool (float)>)>;
    using Done = std::function<void (juce::Result, juce::Array<juce::File>)>;

    ExportThread (Job j, Done d) : juce::Thread ("Stem Splitter export"), job (std::move (j)), done (std::move (d)) {}
    ~ExportThread() override { stopThread (20000); }

    std::atomic<float> progress { 0.0f };
    std::atomic<bool> finished { false };

    void run() override
    {
        juce::Array<juce::File> written;
        auto result = juce::Result::ok();

        try
        {
            result = job (written, [this] (float p) { progress = p; return ! threadShouldExit(); });
        }
        catch (const std::bad_alloc&)
        {
            result = juce::Result::fail ("Not enough memory to export.");
        }

        finished = true;
        juce::MessageManager::callAsync ([d = done, result, written] { d (result, written); });
    }

private:
    Job job;
    Done done;
};

//==============================================================================
StemSplitterEditor::StemSplitterEditor (StemSplitterProcessor& p)
    : AudioProcessorEditor (p), proc (p), header (p), transport (p), exportPanel (p), modelOverlay (p)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (header);
    addAndMakeVisible (transport);
    addAndMakeVisible (exportPanel);
    addAndMakeVisible (dropZone);

    header.onOpenFile = [this] { openFileChooser(); };
    header.onSettings = [this] { if (proc.showAudioSettings) proc.showAudioSettings(); };
    header.onDownloadModel = [this] { showModelOverlay (false); };
    header.onCancelDownload = [this] { proc.getDownloader().cancel(); };

    dropZone.onBrowse = [this] { openFileChooser(); };

    separateButton.getProperties().set ("variant", "primary");
    separateButton.onClick = [this] { separateOrPromptForModel(); };
    addChildComponent (separateButton);

    cancelButton.getProperties().set ("variant", "ghost");
    cancelButton.onClick = [this] { proc.cancelSeparation(); };
    addChildComponent (cancelButton);

    closeFileButton = std::make_unique<IconButton> ("Close", Icons::close());
    closeFileButton->setTooltip ("Close this song");
    closeFileButton->onClick = [this] { proc.closeFile(); };
    addChildComponent (*closeFileButton);

    laneViewport.setViewedComponent (&laneContainer, false);
    laneViewport.setScrollBarsShown (true, false);
    laneViewport.setScrollBarThickness (8);
    addChildComponent (laneViewport);

    exportPanel.onExport = [this] { startExport(); };

    modelOverlay.onLocateFiles = [this] { locateModelFiles(); };
    modelOverlay.onReady = [this] { refreshAll(); };
    addChildComponent (modelOverlay);

    toast = std::make_unique<Toast>();
    addChildComponent (*toast);

    proc.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (980, 640, 3000, 2000);
    setSize (1180, 760);

    refreshAll();
    startTimerHz (30);
}

StemSplitterEditor::~StemSplitterEditor()
{
    stopTimer();
    proc.removeChangeListener (this);
    exportThread.reset();
    setLookAndFeel (nullptr);
}

//==============================================================================
juce::Rectangle<int> StemSplitterEditor::getMainArea() const
{
    return getLocalBounds().withTrimmedTop (headerHeight).withTrimmedBottom (transportHeight)
                           .withTrimmedRight (exportPanelWidth).reduced (20, 16);
}

juce::Rectangle<int> StemSplitterEditor::getFileRowArea() const
{
    return getMainArea().removeFromTop (fileRowHeight);
}

juce::Rectangle<int> StemSplitterEditor::getProgressArea() const
{
    auto row = getFileRowArea();
    row.removeFromRight (36 + 8);   // close button
    const int width = juce::jmin (380, row.getWidth() / 2);
    return row.removeFromRight (width).withSizeKeepingCentre (width, 40);
}

void StemSplitterEditor::resized()
{
    auto r = getLocalBounds();
    header.setBounds (r.removeFromTop (headerHeight));
    transport.setBounds (r.removeFromBottom (transportHeight));
    exportPanel.setBounds (r.removeFromRight (exportPanelWidth));

    auto main = getMainArea();
    dropZone.setBounds (main);

    auto row = main.removeFromTop (fileRowHeight);
    closeFileButton->setBounds (row.removeFromRight (36).withSizeKeepingCentre (36, 36));
    row.removeFromRight (8);

    const auto progressArea = getProgressArea();
    separateButton.setBounds (row.removeFromRight (230).withSizeKeepingCentre (230, 40));
    cancelButton.setBounds (progressArea.withTrimmedLeft (progressArea.getWidth() - 80));

    main.removeFromTop (10);
    laneViewport.setBounds (main);
    layoutLanes();

    modelOverlay.setBounds (getLocalBounds());

    toast->setBounds (getMainArea().withTrimmedBottom (-8).removeFromBottom (44)
                          .withSizeKeepingCentre (juce::jmin (getMainArea().getWidth(), toast->getIdealWidth()), 44));
}

void StemSplitterEditor::paint (juce::Graphics& g)
{
    using namespace Theme::Colours;
    g.fillAll (background);

    const auto source = proc.getSource();

    if (source == nullptr)
        return;

    // File row: name + details
    auto row = getFileRowArea();
    row.removeFromRight (36 + 8 + 240);
    auto nameArea = row.removeFromTop (row.getHeight() / 2 + 4);

    g.setColour (Theme::Colours::text);
    g.setFont (Theme::font (19.0f, true));
    g.drawFittedText (source->file.getFileNameWithoutExtension(), nameArea.withTrimmedBottom (2), juce::Justification::bottomLeft, 1);

    juce::StringArray details;
    details.add (Theme::formatTime (source->getLengthSeconds()));
    const bool fractionalKhz = std::abs (std::fmod (source->sampleRate, 1000.0)) > 0.5;
    details.add (juce::String (source->sampleRate / 1000.0, fractionalKhz ? 1 : 0) + " kHz");
    details.add (juce::String (source->bitsPerSample) + "-bit");
    details.add (source->originalNumChannels == 1 ? "Mono" : source->originalNumChannels == 2 ? "Stereo"
                                                   : juce::String (source->originalNumChannels) + " channels");
    details.add (source->file.getFileExtension().substring (1).toUpperCase());

    g.setColour (textDim);
    g.setFont (Theme::font (12.5f));
    g.drawText (details.joinIntoString (juce::String::fromUTF8 ("   ·   ")), row.withTrimmedTop (4), juce::Justification::topLeft, true);

    // Separation progress
    const auto& job = proc.getSeparationJob();

    if (proc.isSeparating())
    {
        auto area = getProgressArea().withTrimmedRight (88);
        auto bar = area.removeFromBottom (6).toFloat();
        const float p = job.getProgress();

        g.setColour (raised);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, bar.getWidth() * p)), 3.0f);

        juce::String status = job.getStage() + "  " + juce::String (juce::roundToInt (p * 100.0f)) + "%";
        const auto remaining = job.getEstimatedSecondsRemaining();

        if (remaining > 0.0)
            status << juce::String::fromUTF8 ("   ·   about ") << (remaining < 60.0 ? juce::String (juce::jmax (1, (int) remaining)) + " s"
                                                          : Theme::formatTime (remaining)) << " left";

        g.setColour (Theme::Colours::text);
        g.setFont (Theme::font (12.5f, true));
        g.drawText (status, area.withTrimmedBottom (6), juce::Justification::centredLeft, true);
    }
}

//==============================================================================
void StemSplitterEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshAll();

    const auto message = proc.popNotification();

    if (message.isNotEmpty())
        showToast (message);
}

void StemSplitterEditor::refreshAll()
{
    const auto source = proc.getSource();
    const bool hasSource = source != nullptr;
    const bool separating = proc.isSeparating();
    const auto stems = proc.getStems();

    dropZone.setVisible (! hasSource);
    dropZone.setLoading (proc.isLoadingFile());
    laneViewport.setVisible (hasSource);
    closeFileButton->setVisible (hasSource && ! separating);
    separateButton.setVisible (hasSource && ! separating);
    cancelButton.setVisible (hasSource && separating);

    const auto engine = proc.getEngine();
    const auto stemNames = getStemsForEngine (engine);
    juce::String label;

    if (stems != nullptr && stems->engine == engine)
        label = "Separate again";
    else
        label = engine == EngineType::sixStems ? "Separate into 6 stems" : "Separate into 4 stems";

    separateButton.setButtonText (label);
    separateButton.setEnabled (! proc.isLoadingFile());

    header.refresh();
    exportPanel.refresh();
    transport.update();
    modelOverlay.refresh();

    rebuildLanesIfNeeded();
    updateLaneStates();
    repaint();
}

void StemSplitterEditor::rebuildLanesIfNeeded()
{
    const auto source = proc.getSource();
    const auto stems = proc.getStems();
    const auto engine = proc.getEngine();

    const bool needsRebuild = source != shownSource || stems != shownStems || (stems == nullptr && engine != shownEngine);

    if (! needsRebuild)
        return;

    shownSource = source;
    shownStems = stems;
    shownEngine = engine;

    originalLane.reset();
    stemLanes.clear();

    if (source == nullptr)
        return;

    originalLane = std::make_unique<StemLane> (std::nullopt);
    originalLane->setWaveform (&proc.getSourcePeaks());
    originalLane->onSeek = [this] (double f) { seekToFraction (f); };
    laneContainer.addAndMakeVisible (*originalLane);

    for (auto type : displayOrder)
    {
        const StemAudio* stem = nullptr;
        const WaveformPeaks* peaks = nullptr;

        if (stems != nullptr)
        {
            for (size_t i = 0; i < stems->stems.size(); ++i)
                if (stems->stems[i].type == type)
                {
                    stem = &stems->stems[i];
                    peaks = &stems->peaks[i];
                }

            if (stem == nullptr)
                continue;
        }
        else if (! getStemsForEngine (engine).contains (type))
        {
            continue;
        }

        auto* lane = stemLanes.add (new StemLane (type));
        lane->setPlaceholder (stem == nullptr);
        lane->setSubtitle (stem == nullptr ? (type == StemType::other ? juce::String ("Everything else will appear here")
                                                                       : "The " + getStemName (type).toLowerCase() + " will appear here")
                                           : juce::String());
        lane->setShowExportToggle (false);

        if (peaks != nullptr)
            lane->setWaveform (peaks);

        lane->onMute = [this, type] (bool m) { proc.setStemMuted (type, m); };
        lane->onSolo = [this, type] (bool s) { proc.setStemSolo (type, s); };
        lane->onGain = [this, type] (float db) { proc.setStemGain (type, db); };
        lane->onSeek = [this] (double f) { seekToFraction (f); };
        lane->onDragOut = [this, type] { dragStemOut (type); };
        laneContainer.addAndMakeVisible (lane);
    }

    layoutLanes();
}

void StemSplitterEditor::layoutLanes()
{
    const int width = laneViewport.getWidth() - (laneViewport.canScrollVertically() ? 10 : 0);
    const int available = laneViewport.getHeight();
    const int gap = 8;
    const int n = stemLanes.size();

    const int originalHeight = 76;
    const int laneHeight = n > 0 ? juce::jlimit (64, 104, (available - originalHeight - gap - (n * gap)) / n) : 0;
    const int total = originalHeight + n * (laneHeight + gap);

    laneContainer.setSize (juce::jmax (0, total > available ? laneViewport.getWidth() - 10 : laneViewport.getWidth()), total);

    int y = 0;

    if (originalLane != nullptr)
    {
        originalLane->setBounds (0, y, laneContainer.getWidth(), originalHeight);
        y += originalHeight + gap;
    }

    for (auto* lane : stemLanes)
    {
        lane->setBounds (0, y, laneContainer.getWidth(), laneHeight);
        y += laneHeight + gap;
    }

    juce::ignoreUnused (width);
}

void StemSplitterEditor::updateLaneStates()
{
    const auto& mix = proc.getMix();
    const auto source = proc.getSource();

    if (originalLane != nullptr && source != nullptr)
    {
        const bool hasStems = proc.getStems() != nullptr;
        originalLane->setSubtitle (hasStems ? "Original mix (reference)" : juce::String::fromUTF8 ("Original mix · playing"));
        originalLane->setMixState ({}, ! hasStems);
    }

    for (auto* lane : stemLanes)
    {
        const auto type = *lane->getType();
        lane->setMixState (mix.stems[(size_t) toIndex (type)], mix.getEffectiveGain (type) > 0.0f);
    }

    updatePlayheads();
}

void StemSplitterEditor::updatePlayheads()
{
    const auto source = proc.getSource();

    if (source == nullptr || source->getNumSamples() == 0)
        return;

    const double fraction = (double) proc.getPlayPosition() / (double) source->getNumSamples();

    if (originalLane != nullptr)
        originalLane->setPlayhead (fraction);

    for (auto* lane : stemLanes)
        lane->setPlayhead (fraction);
}

void StemSplitterEditor::seekToFraction (double fraction)
{
    if (auto source = proc.getSource())
    {
        proc.seekToSample ((juce::int64) (fraction * source->getNumSamples()));
        updatePlayheads();
        transport.update();
    }
}

void StemSplitterEditor::timerCallback()
{
    ++tick;
    transport.update();
    updatePlayheads();

    if (proc.isSeparating())
        repaint (getFileRowArea());

    // keep button states in sync once the job has wound down
    if (separateButton.isVisible() == proc.isSeparating() && proc.getSource() != nullptr)
        refreshAll();

    if (proc.getDownloader().isDownloading() && tick % 6 == 0)
    {
        header.refresh();
        modelOverlay.refresh();
    }

    if (exportThread != nullptr)
        exportPanel.setBusy (! exportThread->finished.load(), exportThread->progress.load());
}

//==============================================================================
void StemSplitterEditor::openFileChooser()
{
    const auto source = proc.getSource();
    auto start = source != nullptr ? source->file.getParentDirectory()
                                   : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Open an audio file", start, AudioIO::getSupportedFilePattern());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();

                              if (f.existsAsFile())
                                  proc.loadFile (f);
                          });
}

void StemSplitterEditor::separateOrPromptForModel()
{
    if (proc.getSource() == nullptr || proc.isSeparating())
        return;

    if (! ModelManager::isInstalled (proc.getEngine()))
    {
        showModelOverlay (true);
        return;
    }

    proc.startSeparation();
}

void StemSplitterEditor::showModelOverlay (bool separateWhenReady)
{
    const auto engine = proc.getEngine();

    if (engine == EngineType::lite || ModelManager::isInstalled (engine))
        return;

    modelOverlay.shouldSeparateWhenReady = separateWhenReady;
    modelOverlay.show (engine);
}

void StemSplitterEditor::locateModelFiles()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose Demucs model files (ggml-model-*.bin)",
                                                   juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"),
                                                   "*.bin");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto files = fc.getResults();

                              if (files.isEmpty())
                                  return;

                              juce::String error;
                              const int n = ModelManager::importModelFiles (files, error);

                              if (n > 0)
                                  showToast ("Installed " + juce::String (n) + " model file" + (n == 1 ? "" : "s"));
                              else
                                  showToast (error.isNotEmpty() ? error : "No model files were installed.");

                              refreshAll();
                          });
}

//==============================================================================
void StemSplitterEditor::startExport()
{
    const auto source = proc.getSource();
    const auto stems = proc.getStems();

    if (source == nullptr || (exportThread != nullptr && ! exportThread->finished.load()))
        return;

    const auto prefs = proc.getExportPreferences();
    MixRenderer::ExportOptions options;
    options.bitDepth = prefs.bitDepth;
    options.sampleRate = prefs.sampleRate;

    auto startFolder = juce::File (prefs.lastFolder);
    if (! startFolder.isDirectory())
        startFolder = source->file.getParentDirectory();

    const auto songName = AudioIO::makeSafeFileName (source->file.getFileNameWithoutExtension());

    if (prefs.mode == ExportMode::stems)
    {
        if (stems == nullptr)
        {
            showToast ("Separate the song before exporting stems.");
            return;
        }

        juce::Array<StemType> which;
        for (auto type : displayOrder)
            if (prefs.stemSelected[(size_t) toIndex (type)] && stems->find (type) != nullptr)
                which.add (type);

        chooser = std::make_unique<juce::FileChooser> ("Choose where to save the stems", startFolder);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this, source, stems, which, options, songName] (const juce::FileChooser& fc)
                              {
                                  const auto parent = fc.getResult();

                                  if (! parent.isDirectory())
                                      return;

                                  auto p = proc.getExportPreferences();
                                  p.lastFolder = parent.getFullPathName();
                                  proc.setExportPreferences (p);

                                  const auto folder = AudioIO::getNonexistentSibling (parent.getChildFile (songName + " - Stems"));

                                  runExport ([source, stems, which, options, folder] (juce::Array<juce::File>& written, std::function<bool (float)> progress)
                                  {
                                      return MixRenderer::exportStems (*source, *stems, which, folder, options, written, progress);
                                  });
                              });
        return;
    }

    // Custom mix
    const auto mix = proc.getMix();
    const auto description = MixRenderer::describeMix (stems.get(), mix, true);
    const auto defaultFile = startFolder.getChildFile (AudioIO::makeSafeFileName (songName + " (" + description + ")") + ".wav");

    chooser = std::make_unique<juce::FileChooser> ("Save the mix as", defaultFile, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, source, stems, mix, options] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();

                              if (file == juce::File())
                                  return;

                              file = file.withFileExtension ("wav");

                              auto p = proc.getExportPreferences();
                              p.lastFolder = file.getParentDirectory().getFullPathName();
                              proc.setExportPreferences (p);

                              runExport ([source, stems, mix, options, file] (juce::Array<juce::File>& written, std::function<bool (float)> progress)
                              {
                                  auto r = MixRenderer::exportMix (*source, stems.get(), mix, file, options, progress);

                                  if (r.wasOk())
                                      written.add (file);

                                  return r;
                              });
                          });
}

void StemSplitterEditor::runExport (std::function<juce::Result (juce::Array<juce::File>&, std::function<bool (float)>)> job)
{
    exportThread = std::make_unique<ExportThread> (std::move (job), [weakThis = juce::Component::SafePointer<StemSplitterEditor> (this)]
                                                   (juce::Result r, juce::Array<juce::File> written)
    {
        if (weakThis == nullptr)
            return;

        weakThis->exportPanel.setBusy (false);

        if (r.failed())
        {
            weakThis->showToast ("Export failed: " + r.getErrorMessage());
            return;
        }

        if (written.size() == 1)
            weakThis->showToast ("Saved " + written[0].getFileName(), written[0]);
        else if (written.size() > 1)
            weakThis->showToast ("Saved " + juce::String (written.size()) + " stems to \"" + written[0].getParentDirectory().getFileName() + "\"",
                                 written[0]);
    });

    exportPanel.setBusy (true, 0.0f);
    exportThread->startThread();
}

void StemSplitterEditor::dragStemOut (StemType type)
{
    const auto source = proc.getSource();
    const auto stems = proc.getStems();

    if (source == nullptr || stems == nullptr || stems->find (type) == nullptr)
        return;

    if (dragOutCacheOwner != stems.get())
    {
        dragOutCache.clear();
        dragOutCacheOwner = stems.get();
    }

    auto file = dragOutCache[type];

    if (! file.existsAsFile())
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("Stem Splitter")
                             .getChildFile (juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
        const auto songName = AudioIO::makeSafeFileName (source->file.getFileNameWithoutExtension());
        file = dir.getChildFile (songName + " - " + getStemName (type) + ".wav");

        MixRenderer::ExportOptions options;
        options.bitDepth = proc.getExportPreferences().bitDepth;
        options.sampleRate = proc.getExportPreferences().sampleRate;

        double rate = 0.0;
        const auto audio = MixRenderer::prepareForExport (stems->find (type)->buffer, *source, options, rate);
        const auto r = AudioIO::writeWav (file, audio, { options.bitDepth, rate });

        if (r.failed())
        {
            showToast (r.getErrorMessage());
            return;
        }

        dragOutCache[type] = file;
    }

    performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void StemSplitterEditor::showToast (const juce::String& message, const juce::File& revealTarget)
{
    toast->show (message, revealTarget);
    resized();
}

//==============================================================================
bool StemSplitterEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const juce::File file (f);

        if (file.hasFileExtension ("wav;wave;aif;aiff;flac;mp3;m4a;caf;ogg") || ModelManager::looksLikeModelFile (file))
            return true;
    }

    return false;
}

void StemSplitterEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    fileDragOver = true;
    dropZone.setDragHighlight (true);
    repaint();
}

void StemSplitterEditor::fileDragExit (const juce::StringArray&)
{
    fileDragOver = false;
    dropZone.setDragHighlight (false);
    repaint();
}

void StemSplitterEditor::filesDropped (const juce::StringArray& files, int, int)
{
    fileDragOver = false;
    dropZone.setDragHighlight (false);

    // Model files dropped onto the window are installed
    juce::Array<juce::File> models;
    for (const auto& f : files)
        if (ModelManager::looksLikeModelFile (juce::File (f)))
            models.add (juce::File (f));

    if (! models.isEmpty())
    {
        juce::String error;
        const int n = ModelManager::importModelFiles (models, error);
        showToast (n > 0 ? "Installed " + juce::String (n) + " model file" + (n == 1 ? "" : "s") : error);
        refreshAll();
        return;
    }

    for (const auto& f : files)
    {
        const juce::File file (f);

        if (AudioIO::isSupportedFile (file))
        {
            proc.loadFile (file);
            return;
        }
    }

    showToast ("That file type isn't supported. Drop a WAV, AIFF, FLAC or MP3 file.");
}

bool StemSplitterEditor::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = juce::ModifierKeys::commandModifier;

    if (key == juce::KeyPress::spaceKey)
    {
        proc.togglePlayback();
        transport.update();
        return true;
    }

    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::homeKey)
    {
        proc.setPlaying (false);
        proc.seekToStart();
        return true;
    }

    if (key == juce::KeyPress ('l') || key == juce::KeyPress ('L'))
    {
        proc.setLooping (! proc.isLooping());
        return true;
    }

    if (key == juce::KeyPress ('o', cmd, 0))
    {
        openFileChooser();
        return true;
    }

    if (key == juce::KeyPress ('e', cmd, 0))
    {
        startExport();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        if (modelOverlay.isVisible() && ! proc.getDownloader().isDownloading())
            modelOverlay.hide();
        else if (proc.isSeparating())
            proc.cancelSeparation();

        return true;
    }

    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
    {
        if (auto source = proc.getSource())
        {
            const auto step = (juce::int64) (5.0 * source->sampleRate) * (key == juce::KeyPress::leftKey ? -1 : 1);
            proc.seekToSample (proc.getPlayPosition() + step);
        }

        return true;
    }

    return false;
}

} // namespace stemsplitter
