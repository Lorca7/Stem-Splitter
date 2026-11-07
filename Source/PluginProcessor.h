#pragma once

#include "Core/MixRenderer.h"
#include "Core/ModelManager.h"
#include "Core/SeparationJob.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace stemsplitter
{

enum class ExportMode : int { stems = 0, mix };

/** User preferences for exporting, remembered with the plug-in state. */
struct ExportPreferences
{
    ExportMode mode = ExportMode::stems;
    AudioIO::BitDepth bitDepth = AudioIO::BitDepth::int24;
    double sampleRate = 0.0;                 ///< 0 = same as source
    std::array<bool, numStemTypes> stemSelected { true, true, true, true, true, true };
    juce::String lastFolder;
};

/** Owns the loaded song, the separation, the stem mixer and preview playback.
    Everything except processBlock() runs on the message thread. */
class StemSplitterProcessor final : public juce::AudioProcessor,
                                    public juce::ChangeBroadcaster
{
public:
    explicit StemSplitterProcessor (bool isStandalone = false);
    ~StemSplitterProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return JucePlugin_Name; }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 0.0; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Song
    void loadFile (const juce::File& file);

    /** Installs already-decoded audio as the current song (used by loadFile and by tests). */
    void adoptSource (std::shared_ptr<const SourceAudio> audio, WaveformPeaks peaks);
    void closeFile();
    bool isLoadingFile() const noexcept                     { return loading; }
    std::shared_ptr<const SourceAudio> getSource() const    { return source; }
    std::shared_ptr<const StemSet> getStems() const         { return stems; }
    const WaveformPeaks& getSourcePeaks() const noexcept    { return sourcePeaks; }

    // Separation
    EngineType getEngine() const noexcept                   { return engine; }
    void setEngine (EngineType e);
    bool canSeparate() const;
    void startSeparation();
    void cancelSeparation();
    const SeparationJob& getSeparationJob() const noexcept  { return separationJob; }
    bool isSeparating() const noexcept                      { return separationJob.isRunning(); }

    // Models
    ModelDownloader& getDownloader() noexcept               { return downloader; }
    void downloadModel (EngineType e);

    // Mixer (what you hear, and what a "mix" export contains)
    const MixSettings& getMix() const noexcept              { return mix; }
    void setMix (const MixSettings& newMix);
    void setStemMuted (StemType t, bool muted);
    void setStemSolo (StemType t, bool solo);
    void setStemGain (StemType t, float gainDb);
    void setFrequencyCut (const FrequencyCutSettings& cut);

    // Export
    const ExportPreferences& getExportPreferences() const noexcept { return exportPrefs; }
    void setExportPreferences (const ExportPreferences& prefs);

    // Transport
    void setPlaying (bool shouldPlay);
    bool isPlaying() const noexcept                         { return playing.load(); }
    void togglePlayback()                                   { setPlaying (! isPlaying()); }
    void seekToSample (juce::int64 sourceSample);
    void seekToStart()                                      { seekToSample (0); }
    juce::int64 getPlayPosition() const noexcept            { return playPosition.load(); }
    void setLooping (bool shouldLoop)                       { looping = shouldLoop; sendChangeMessage(); }
    bool isLooping() const noexcept                         { return looping.load(); }

    /** Last error/notice for the UI to show. Cleared once read. */
    juce::String popNotification();

    /** Set by the standalone app: opens the audio device settings. */
    std::function<void()> showAudioSettings;

    bool isStandaloneApp() const noexcept                   { return standalone; }

private:
    //==============================================================================
    struct PlaybackState
    {
        std::shared_ptr<const SourceAudio> source;
        std::shared_ptr<const StemSet> stems;
    };

    void publishPlaybackState();
    juce::int64 getLogicalPosition() const noexcept;
    void publishMix();
    void notify (const juce::String& message);
    void mixSourceBlock (const PlaybackState& state, juce::int64 start, int numSamples,
                         const std::array<float, numStemTypes>& targetGains, bool foldToMono);

    //==============================================================================
    const bool standalone;

    std::shared_ptr<const SourceAudio> source;
    std::shared_ptr<const StemSet> stems;
    WaveformPeaks sourcePeaks;
    juce::File pendingFile;
    std::atomic<bool> loading { false };
    std::unique_ptr<juce::Thread> loaderThread;

    EngineType engine = EngineType::fourStems;
    SeparationJob separationJob;
    ModelDownloader downloader;

    MixSettings mix;
    ExportPreferences exportPrefs;
    juce::String notification;

    // Shared with the audio thread
    juce::SpinLock playbackLock;
    PlaybackState playback;                                    // guarded by playbackLock
    std::array<std::atomic<float>, numStemTypes> stemGains;    // effective linear gains
    juce::SpinLock cutLock;
    FrequencyCutSettings cutForAudio;                          // guarded by cutLock
    std::atomic<bool> playing { false }, looping { false };
    std::atomic<juce::int64> playPosition { 0 }, seekRequest { -1 };
    std::atomic<bool> reachedEnd { false };

    // Audio thread only
    juce::AudioBuffer<float> mixBuffer;
    std::array<float, numStemTypes> currentGains {};
    std::array<juce::LagrangeInterpolator, 2> interpolators;
    FrequencyCutProcessor cutProcessor;
    double deviceRate = 44100.0;
    double readPosition = 0.0;
    bool needsPriming = true;
    bool lastBlockWasSameRate = true;
    static constexpr double interpolatorLatency = (double) juce::LagrangeInterpolator::getBaseLatency();
    float fade = 0.0f;   // de-click fade when starting/stopping

    JUCE_DECLARE_WEAK_REFERENCEABLE (StemSplitterProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSplitterProcessor)
};

} // namespace stemsplitter
