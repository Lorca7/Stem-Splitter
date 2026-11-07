#pragma once

#include "Session.h"

namespace stemsplitter
{

/** Runs a separation on a background thread: converts to the engine's format, separates, converts the
    stems back to the source's sample rate and builds waveform overviews. */
class SeparationJob : private juce::Thread
{
public:
    SeparationJob();
    ~SeparationJob() override;

    void start (std::shared_ptr<const SourceAudio> source, EngineType engine);
    void cancel();

    bool isRunning() const noexcept          { return isThreadRunning() && ! finished.load(); }
    float getProgress() const noexcept       { return progress.load(); }
    juce::String getStage() const;
    double getElapsedSeconds() const noexcept;
    EngineType getEngine() const noexcept    { return engine; }

    /** Seconds remaining, or < 0 if unknown yet. */
    double getEstimatedSecondsRemaining() const noexcept;

    /** Called on the message thread. On failure, stems is null and result has the reason
        ("Cancelled" if the user cancelled). */
    std::function<void (std::shared_ptr<const StemSet> stems, juce::Result result)> onFinished;

    /** Runs a separation synchronously on the calling thread (used by the command-line tool). */
    static juce::Result runSynchronously (const SourceAudio& source, EngineType engine, StemSet& result,
                                          SeparationContext& context);

    static int getRecommendedThreadCount (EngineType engine);

private:
    void run() override;

    std::shared_ptr<const SourceAudio> source;
    EngineType engine = EngineType::fourStems;
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> finished { true };
    std::atomic<double> startTime { 0.0 };
    mutable juce::CriticalSection stageLock;
    juce::String stage;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SeparationJob)
};

} // namespace stemsplitter
