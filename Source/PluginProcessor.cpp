#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace stemsplitter
{

namespace
{
    /** Decodes a file on a background thread and hands the result back on the message thread. */
    class FileLoaderThread final : public juce::Thread
    {
    public:
        using Callback = std::function<void (std::shared_ptr<SourceAudio>, WaveformPeaks, juce::Result)>;

        FileLoaderThread (juce::File f, Callback cb)
            : juce::Thread ("Stem Splitter file loader"), file (std::move (f)), callback (std::move (cb)) {}

        ~FileLoaderThread() override { stopThread (10000); }

        void run() override
        {
            auto audio = std::make_shared<SourceAudio>();
            auto result = AudioIO::loadFile (file, *audio, [this] (float) { return ! threadShouldExit(); });
            WaveformPeaks peaks;

            if (result.wasOk())
                peaks = WaveformPeaks (audio->buffer);

            if (threadShouldExit())
                return;

            juce::MessageManager::callAsync ([cb = callback, audio, peaks = std::move (peaks), result]() mutable
            {
                cb (result.wasOk() ? audio : nullptr, std::move (peaks), result);
            });
        }

    private:
        juce::File file;
        Callback callback;
    };
}

//==============================================================================
StemSplitterProcessor::StemSplitterProcessor (bool isStandalone)
    : AudioProcessor (isStandalone
                          ? BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          : BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                             .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      standalone (isStandalone)
{
    for (auto& g : stemGains)
        g = 1.0f;

    currentGains.fill (1.0f);

    separationJob.onFinished = [this] (std::shared_ptr<const StemSet> result, juce::Result r)
    {
        if (result != nullptr)
        {
            stems = std::move (result);
            publishPlaybackState();

            const auto secs = stems->secondsTaken;
            notify ("Separated into " + juce::String ((int) stems->stems.size()) + " stems in "
                    + (secs < 60.0 ? juce::String (secs, 1) + " s" : juce::String (secs / 60.0, 1) + " min"));
        }
        else if (r.getErrorMessage() != "Cancelled")
        {
            notify ("Separation failed: " + r.getErrorMessage());
        }

        sendChangeMessage();
    };

    downloader.onFinished = [this] (EngineType e, juce::String error)
    {
        if (error.isEmpty())
            notify (getEngineName (e) + " model installed");
        else if (error != "Download cancelled.")
            notify (error);

        sendChangeMessage();
    };

    // Prefer an engine that's ready to go on first launch.
    if (! ModelManager::isInstalled (engine))
        for (auto e : { EngineType::sixStems, EngineType::fourStemsHQ })
            if (ModelManager::isInstalled (e))
            {
                engine = e;
                break;
            }
}

StemSplitterProcessor::~StemSplitterProcessor()
{
    masterReference.clear();
    loaderThread.reset();
    separationJob.cancel();
    downloader.cancel();
}

//==============================================================================
bool StemSplitterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == out;
}

void StemSplitterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    deviceRate = sampleRate > 0 ? sampleRate : 44100.0;
    mixBuffer.setSize (2, juce::jmax (512, samplesPerBlock) * 8 + 64, false, true, false);
    cutProcessor.prepare (deviceRate, 2);

    for (auto& i : interpolators)
        i.reset();

    needsPriming = true;
}

void StemSplitterProcessor::releaseResources() {}

void StemSplitterProcessor::mixSourceBlock (const PlaybackState& state, juce::int64 start, int numSamples,
                                            const std::array<float, numStemTypes>& targetGains, bool foldToMono)
{
    mixBuffer.clear (0, numSamples);

    const auto total = (juce::int64) state.source->getNumSamples();

    // Positions before the start (interpolator priming) read as silence.
    const int offset = start < 0 ? (int) juce::jmin ((juce::int64) numSamples, -start) : 0;
    start += offset;

    if (start >= total || offset >= numSamples)
        return;

    const int n = (int) juce::jmin ((juce::int64) (numSamples - offset), total - start);

    if (state.stems != nullptr && ! state.stems->stems.empty())
    {
        for (const auto& stem : state.stems->stems)
        {
            const auto index = (size_t) toIndex (stem.type);
            const float from = currentGains[index];
            const float to = targetGains[index];

            if (from <= 0.0f && to <= 0.0f)
                continue;

            for (int ch = 0; ch < 2; ++ch)
                mixBuffer.addFromWithRamp (ch, offset,
                                           stem.buffer.getReadPointer (juce::jmin (ch, stem.buffer.getNumChannels() - 1), (int) start),
                                           n, from, to);
        }
    }
    else
    {
        for (int ch = 0; ch < 2; ++ch)
            mixBuffer.copyFrom (ch, offset, state.source->buffer, juce::jmin (ch, state.source->buffer.getNumChannels() - 1), (int) start, n);
    }

    if (foldToMono)
    {
        mixBuffer.addFrom (0, offset, mixBuffer, 1, offset, n);
        mixBuffer.applyGain (0, offset, n, 0.5f);
    }
}

void StemSplitterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numOut = buffer.getNumSamples();
    buffer.clear();   // the plug-in plays the song; incoming audio is not passed through

    const auto seek = seekRequest.exchange (-1);

    if (seek >= 0)
    {
        readPosition = (double) seek;

        for (auto& i : interpolators)
            i.reset();

        needsPriming = true;
        cutProcessor.reset();
    }

    const bool shouldPlay = playing.load();

    if (! shouldPlay && fade <= 0.0f)
    {
        playPosition = getLogicalPosition();
        return;
    }

    const juce::SpinLock::ScopedTryLockType lock (playbackLock);

    if (! lock.isLocked() || playback.source == nullptr || mixBuffer.getNumSamples() == 0)
        return;

    const auto& state = playback;
    const double ratio = state.source->sampleRate / deviceRate;
    const bool sameRate = std::abs (ratio - 1.0) < 1.0e-9;
    lastBlockWasSameRate = sameRate;
    const auto total = (juce::int64) state.source->getNumSamples();
    const int capacity = mixBuffer.getNumSamples();
    const int maxOutPerPass = juce::jmax (1, (int) std::floor ((capacity - 8) / juce::jmax (1.0, ratio)));
    const bool loop = looping.load();
    const int numCh = juce::jmin (2, buffer.getNumChannels());

    std::array<float, numStemTypes> targetGains;
    for (size_t i = 0; i < targetGains.size(); ++i)
        targetGains[i] = stemGains[i].load();

    int done = 0;
    bool hitEnd = false;

    while (done < numOut)
    {
        if ((juce::int64) readPosition >= total)
        {
            if (loop && total > 0)
            {
                readPosition = 0.0;

                for (auto& i : interpolators)
                    i.reset();

                needsPriming = true;
                continue;
            }

            hitEnd = true;
            break;
        }

        // The interpolator's output trails the samples fed to it by its latency, so after a reset we
        // start feeding that many samples ahead; the audio you hear then lines up with the playhead.
        if (needsPriming)
        {
            if (! sameRate)
                readPosition += interpolatorLatency;

            needsPriming = false;
        }

        int n = juce::jmin (numOut - done, maxOutPerPass);

        // When looping, stop exactly at the end so the wrap-around is seamless.
        if (loop)
            n = juce::jmax (1, juce::jmin (n, (int) std::ceil ((double) (total - (juce::int64) readPosition) / ratio)));

        const auto start = (juce::int64) readPosition;

        if (sameRate)
        {
            mixSourceBlock (state, start, n, targetGains, numCh == 1);

            for (int ch = 0; ch < numCh; ++ch)
                buffer.copyFrom (ch, done, mixBuffer, ch, 0, n);

            readPosition += n;
        }
        else
        {
            const int needed = juce::jmin (capacity, (int) std::ceil (n * ratio) + 6);
            mixSourceBlock (state, start, needed, targetGains, numCh == 1);
            int used = 0;

            for (int ch = 0; ch < numCh; ++ch)
                used = interpolators[(size_t) ch].process (ratio, mixBuffer.getReadPointer (ch), buffer.getWritePointer (ch, done), n);

            readPosition += used;
        }

        currentGains = targetGains;   // ramps are applied once per pass
        done += n;
    }

    currentGains = targetGains;

    // Frequency cut preview
    {
        FrequencyCutSettings cut;
        bool haveCut = false;

        {
            const juce::SpinLock::ScopedTryLockType cutLockGuard (cutLock);

            if (cutLockGuard.isLocked())
            {
                cut = cutForAudio;
                haveCut = true;
            }
        }

        if (haveCut)
            cutProcessor.setSettings (cut);

        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), numCh, numOut);
        cutProcessor.process (view, 0, numOut);
    }

    // De-click fades when starting/stopping
    const float fadeStep = (float) numOut / (float) (0.008 * deviceRate);
    const float targetFade = shouldPlay && ! hitEnd ? 1.0f : 0.0f;
    float newFade = fade;

    if (targetFade > fade)
        newFade = juce::jmin (1.0f, fade + fadeStep);
    else if (targetFade < fade)
        newFade = juce::jmax (0.0f, fade - fadeStep);

    if (! juce::approximatelyEqual (fade, 1.0f) || ! juce::approximatelyEqual (newFade, 1.0f))
        buffer.applyGainRamp (0, numOut, fade, newFade);

    fade = newFade;

    if (hitEnd)
    {
        playing = false;
        readPosition = 0.0;
        fade = 0.0f;
        reachedEnd = true;
    }

    playPosition = getLogicalPosition();
}

juce::int64 StemSplitterProcessor::getLogicalPosition() const noexcept
{
    // When resampling, the read position runs ahead of what's audible by the interpolator's latency.
    const bool primed = ! needsPriming && ! lastBlockWasSameRate;
    return juce::jmax ((juce::int64) 0, (juce::int64) std::llround (readPosition - (primed ? interpolatorLatency : 0.0)));
}

//==============================================================================
void StemSplitterProcessor::loadFile (const juce::File& file)
{
    if (! file.existsAsFile())
    {
        notify ("\"" + file.getFileName() + "\" could not be found.");
        return;
    }

    cancelSeparation();
    loaderThread.reset();
    loading = true;
    pendingFile = file;
    sendChangeMessage();

    auto weakThis = juce::WeakReference<StemSplitterProcessor> (this);

    loaderThread = std::make_unique<FileLoaderThread> (file, [weakThis] (std::shared_ptr<SourceAudio> audio, WaveformPeaks peaks, juce::Result r)
    {
        auto* self = weakThis.get();

        if (self == nullptr)
            return;

        self->loading = false;

        if (audio == nullptr)
        {
            self->notify (r.getErrorMessage());
            self->sendChangeMessage();
            return;
        }

        self->adoptSource (std::move (audio), std::move (peaks));
    });

    loaderThread->startThread();
}

void StemSplitterProcessor::adoptSource (std::shared_ptr<const SourceAudio> audio, WaveformPeaks peaks)
{
    setPlaying (false);
    source = std::move (audio);
    sourcePeaks = std::move (peaks);
    stems.reset();
    publishPlaybackState();
    seekToStart();
    sendChangeMessage();
}

void StemSplitterProcessor::closeFile()
{
    cancelSeparation();
    setPlaying (false);
    source.reset();
    stems.reset();
    sourcePeaks = {};
    publishPlaybackState();
    seekToStart();
    sendChangeMessage();
}

void StemSplitterProcessor::publishPlaybackState()
{
    PlaybackState newState { source, stems };

    {
        const juce::SpinLock::ScopedLockType lock (playbackLock);
        std::swap (playback, newState);
    }
    // the previous state (and possibly its audio) is released here, off the audio thread
}

//==============================================================================
void StemSplitterProcessor::setEngine (EngineType e)
{
    if (e == engine || isSeparating())
        return;

    engine = e;
    sendChangeMessage();
}

bool StemSplitterProcessor::canSeparate() const
{
    return source != nullptr && ! isSeparating() && ! loading && ModelManager::isInstalled (engine);
}

void StemSplitterProcessor::startSeparation()
{
    if (source == nullptr || isSeparating())
        return;

    if (! ModelManager::isInstalled (engine))
    {
        notify ("The " + getEngineName (engine) + " model isn't installed yet.");
        return;
    }

    separationJob.start (source, engine);
    sendChangeMessage();
}

void StemSplitterProcessor::cancelSeparation()
{
    separationJob.cancel();
}

void StemSplitterProcessor::downloadModel (EngineType e)
{
    if (e == EngineType::lite || downloader.isDownloading())
        return;

    downloader.start (e);
    sendChangeMessage();
}

//==============================================================================
void StemSplitterProcessor::setMix (const MixSettings& newMix)
{
    mix = newMix;
    publishMix();
    sendChangeMessage();
}

void StemSplitterProcessor::setStemMuted (StemType t, bool muted)
{
    auto m = mix;
    m.stems[(size_t) toIndex (t)].muted = muted;
    setMix (m);
}

void StemSplitterProcessor::setStemSolo (StemType t, bool solo)
{
    auto m = mix;
    m.stems[(size_t) toIndex (t)].solo = solo;
    setMix (m);
}

void StemSplitterProcessor::setStemGain (StemType t, float gainDb)
{
    auto m = mix;
    m.stems[(size_t) toIndex (t)].gainDb = gainDb;
    setMix (m);
}

void StemSplitterProcessor::setFrequencyCut (const FrequencyCutSettings& cut)
{
    auto m = mix;
    m.cut = cut;
    setMix (m);
}

void StemSplitterProcessor::setExportPreferences (const ExportPreferences& prefs)
{
    exportPrefs = prefs;
    publishMix();   // the frequency cut is only heard in "mix" mode
    sendChangeMessage();
}

void StemSplitterProcessor::publishMix()
{
    for (int i = 0; i < numStemTypes; ++i)
        stemGains[(size_t) i] = mix.getEffectiveGain (fromIndex (i));

    auto cut = mix.cut;
    cut.enabled = cut.enabled && exportPrefs.mode == ExportMode::mix;

    const juce::SpinLock::ScopedLockType lock (cutLock);
    cutForAudio = cut;
}

//==============================================================================
void StemSplitterProcessor::setPlaying (bool shouldPlay)
{
    if (shouldPlay && source == nullptr)
        return;

    playing = shouldPlay;
    sendChangeMessage();
}

void StemSplitterProcessor::seekToSample (juce::int64 sample)
{
    const auto total = source != nullptr ? (juce::int64) source->getNumSamples() : (juce::int64) 0;
    const auto clamped = juce::jlimit ((juce::int64) 0, juce::jmax ((juce::int64) 0, total - 1), sample);
    seekRequest = clamped;
    playPosition = clamped;   // so the UI updates immediately, even while stopped
}

void StemSplitterProcessor::notify (const juce::String& message)
{
    notification = message;
    sendChangeMessage();
}

juce::String StemSplitterProcessor::popNotification()
{
    auto n = notification;
    notification.clear();
    return n;
}

//==============================================================================
void StemSplitterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("StemSplitter");
    xml.setAttribute ("version", 1);

    const auto file = source != nullptr ? source->file : pendingFile;
    xml.setAttribute ("file", file.getFullPathName());
    xml.setAttribute ("engine", (int) engine);
    xml.setAttribute ("loop", looping.load());

    auto* mixXml = xml.createNewChildElement ("Mix");

    for (int i = 0; i < numStemTypes; ++i)
    {
        const auto& s = mix.stems[(size_t) i];
        auto* stemXml = mixXml->createNewChildElement ("Stem");
        stemXml->setAttribute ("type", getStemName (fromIndex (i)));
        stemXml->setAttribute ("gain", (double) s.gainDb);
        stemXml->setAttribute ("mute", s.muted);
        stemXml->setAttribute ("solo", s.solo);
    }

    auto* cutXml = xml.createNewChildElement ("FrequencyCut");
    cutXml->setAttribute ("enabled", mix.cut.enabled);
    cutXml->setAttribute ("mode", (int) mix.cut.mode);
    cutXml->setAttribute ("low", (double) mix.cut.lowHz);
    cutXml->setAttribute ("high", (double) mix.cut.highHz);
    cutXml->setAttribute ("amount", (double) mix.cut.amount);

    auto* exportXml = xml.createNewChildElement ("Export");
    exportXml->setAttribute ("mode", (int) exportPrefs.mode);
    exportXml->setAttribute ("bits", (int) exportPrefs.bitDepth);
    exportXml->setAttribute ("rate", exportPrefs.sampleRate);
    exportXml->setAttribute ("folder", exportPrefs.lastFolder);

    juce::String selected;
    for (auto b : exportPrefs.stemSelected)
        selected << (b ? "1" : "0");
    exportXml->setAttribute ("stems", selected);

    copyXmlToBinary (xml, destData);
}

void StemSplitterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName ("StemSplitter"))
        return;

    engine = (EngineType) juce::jlimit (0, numEngineTypes - 1, xml->getIntAttribute ("engine", (int) engine));
    looping = xml->getBoolAttribute ("loop", false);

    MixSettings m;

    if (auto* mixXml = xml->getChildByName ("Mix"))
    {
        for (auto* stemXml : mixXml->getChildWithTagNameIterator ("Stem"))
        {
            for (int i = 0; i < numStemTypes; ++i)
            {
                if (stemXml->getStringAttribute ("type") == getStemName (fromIndex (i)))
                {
                    auto& s = m.stems[(size_t) i];
                    s.gainDb = (float) stemXml->getDoubleAttribute ("gain", 0.0);
                    s.muted = stemXml->getBoolAttribute ("mute", false);
                    s.solo = stemXml->getBoolAttribute ("solo", false);
                }
            }
        }
    }

    if (auto* cutXml = xml->getChildByName ("FrequencyCut"))
    {
        m.cut.enabled = cutXml->getBoolAttribute ("enabled", false);
        m.cut.mode = (FrequencyCutSettings::Mode) juce::jlimit (0, 2, cutXml->getIntAttribute ("mode", 1));
        m.cut.lowHz = (float) cutXml->getDoubleAttribute ("low", 250.0);
        m.cut.highHz = (float) cutXml->getDoubleAttribute ("high", 4000.0);
        m.cut.amount = (float) cutXml->getDoubleAttribute ("amount", 1.0);
    }

    if (auto* exportXml = xml->getChildByName ("Export"))
    {
        exportPrefs.mode = exportXml->getIntAttribute ("mode", 0) == 1 ? ExportMode::mix : ExportMode::stems;
        const int bits = exportXml->getIntAttribute ("bits", 24);
        exportPrefs.bitDepth = bits == 16 ? AudioIO::BitDepth::int16 : bits == 32 ? AudioIO::BitDepth::float32 : AudioIO::BitDepth::int24;
        exportPrefs.sampleRate = exportXml->getDoubleAttribute ("rate", 0.0);
        exportPrefs.lastFolder = exportXml->getStringAttribute ("folder");

        const auto selected = exportXml->getStringAttribute ("stems");
        for (int i = 0; i < numStemTypes && i < selected.length(); ++i)
            exportPrefs.stemSelected[(size_t) i] = selected[i] == '1';
    }

    setMix (m);

    const juce::File file (xml->getStringAttribute ("file"));

    if (file.existsAsFile() && (source == nullptr || source->file != file))
        loadFile (file);

    sendChangeMessage();
}

//==============================================================================
juce::AudioProcessorEditor* StemSplitterProcessor::createEditor()
{
    return new StemSplitterEditor (*this);
}

} // namespace stemsplitter

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    const bool standalone = juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone;
    return new stemsplitter::StemSplitterProcessor (standalone);
}
