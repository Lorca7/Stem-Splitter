#pragma once

#include "NoiseCleanup.h"
#include "Session.h"

#include <juce_events/juce_events.h>
#include <optional>

namespace stemsplitter
{

/** Runs the clean-up in the background.

    - Analyses each newly loaded song once (spectrum, automatic noise profile, hum, tones).
    - Renders cleaned copies of the original and of every stem whenever the settings, the song or the
      stems change. Changes are debounced while you drag a slider, and a newer request cancels an
      older render, so it never wastes CPU on settings you've moved past.
    - Playback just swaps to the cleaned buffers: no real-time processing cost at all.

    All public functions are called on the message thread. */
class CleanupEngine : private juce::Thread, private juce::Timer
{
public:
    CleanupEngine();
    ~CleanupEngine() override;

    struct Result
    {
        std::shared_ptr<const SourceAudio> inputSource;   ///< what this was rendered from
        std::shared_ptr<const StemSet> inputStems;
        CleanupSettings settings;
        int profileVersion = 0;
        float humHz = 0.0f;

        std::shared_ptr<const SourceAudio> source;        ///< cleaned audio
        std::shared_ptr<const StemSet> stems;
        WaveformPeaks sourcePeaks;
    };

    void setInputs (std::shared_ptr<const SourceAudio> source, std::shared_ptr<const StemSet> stems);
    void setSettings (const CleanupSettings& settings);
    void setCustomProfile (const NoiseProfile& profile);
    void clearCustomProfile();

    bool hasCustomProfile() const noexcept                           { return customProfile.isValid(); }
    const NoiseProfile& getCustomProfile() const noexcept            { return customProfile; }

    /** True when the latest result was rendered from the current inputs with these settings. */
    bool isResultCurrent (const CleanupSettings& s) const            { return resultMatches (s); }
    std::shared_ptr<const CleanupAnalysis> getAnalysis() const       { return analysis; }
    std::shared_ptr<const Result> getResult() const                  { return result; }

    bool isAnalysing() const noexcept                                { return analysing.load(); }
    bool isRendering() const noexcept                                { return rendering.load() || isTimerRunning(); }
    float getProgress() const noexcept                               { return progress.load(); }

    /** Called on the message thread whenever the analysis or the rendered result changes. */
    std::function<void()> onChange;

private:
    struct Job
    {
        std::shared_ptr<const SourceAudio> source;
        std::shared_ptr<const StemSet> stems;
        CleanupSettings settings;
        NoiseProfile customProfile;
        std::shared_ptr<const CleanupAnalysis> analysis;
        int profileVersion = 0;
        bool needsAnalysis = false, render = false;
        juce::uint32 generation = 0;
    };

    void timerCallback() override;
    void run() override;
    void schedule (int delayMs);
    bool resultMatches (const CleanupSettings& s) const;

    std::shared_ptr<const SourceAudio> source;
    std::shared_ptr<const StemSet> stems;
    CleanupSettings settings;
    NoiseProfile customProfile;
    std::shared_ptr<const CleanupAnalysis> analysis;
    std::shared_ptr<const SourceAudio> analysedSource, analysisRequested;
    int profileVersion = 0;
    std::shared_ptr<const Result> result;

    juce::CriticalSection jobLock;
    std::optional<Job> pendingJob;
    std::atomic<juce::uint32> generation { 0 };
    std::atomic<bool> analysing { false }, rendering { false };
    std::atomic<float> progress { 0.0f };
    juce::WaitableEvent wakeUp;

    JUCE_DECLARE_WEAK_REFERENCEABLE (CleanupEngine)
};

} // namespace stemsplitter
