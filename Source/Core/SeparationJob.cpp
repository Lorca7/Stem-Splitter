#include "SeparationJob.h"

#include <juce_events/juce_events.h>

namespace stemsplitter
{

SeparationJob::SeparationJob() : juce::Thread ("Stem Splitter separation") {}

SeparationJob::~SeparationJob()
{
    masterReference.clear();
    stopThread (30000);
}

void SeparationJob::start (std::shared_ptr<const SourceAudio> newSource, EngineType newEngine)
{
    if (isThreadRunning())
    {
        if (! finished.load())
            return;

        waitForThreadToExit (-1);   // the previous run has posted its result and is just exiting
    }

    finished = false;
    source = std::move (newSource);
    engine = newEngine;
    progress = 0.0f;
    startTime = juce::Time::getMillisecondCounterHiRes();

    {
        const juce::ScopedLock sl (stageLock);
        stage = "Starting";
    }

    startThread (juce::Thread::Priority::normal);
}

void SeparationJob::cancel()
{
    signalThreadShouldExit();
}

juce::String SeparationJob::getStage() const
{
    const juce::ScopedLock sl (stageLock);
    return stage;
}

double SeparationJob::getElapsedSeconds() const noexcept
{
    return (juce::Time::getMillisecondCounterHiRes() - startTime.load()) / 1000.0;
}

double SeparationJob::getEstimatedSecondsRemaining() const noexcept
{
    const auto p = (double) progress.load();
    const auto elapsed = getElapsedSeconds();

    if (p < 0.03 || elapsed < 2.0)
        return -1.0;

    return elapsed * (1.0 - p) / p;
}

int SeparationJob::getRecommendedThreadCount (EngineType e)
{
    const int cores = juce::jmax (1, juce::SystemStats::getNumPhysicalCpus());

    if (e == EngineType::lite)
        return juce::jlimit (1, 16, juce::SystemStats::getNumCpus());

    // Each Demucs worker needs a few hundred MB of working memory.
    const int memoryLimited = juce::jmax (1, juce::SystemStats::getMemorySizeInMegabytes() / 1500);
    return juce::jlimit (1, 12, juce::jmin (cores, memoryLimited));
}

juce::Result SeparationJob::runSynchronously (const SourceAudio& src, EngineType eng, StemSet& result,
                                              SeparationContext& context)
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto report = context.reportProgress;

    auto mapped = [&report] (float start, float span)
    {
        return [&report, start, span] (float p, const juce::String& s) { report (start + span * p, s); };
    };

    // 1. Engine input: stereo @ 44.1 kHz
    report (0.0f, "Preparing audio");
    auto input = AudioIO::withChannelCount (src.buffer, 2);

    if (std::abs (src.sampleRate - engineSampleRate) > 0.5)
        input = AudioIO::resample (input, src.sampleRate, engineSampleRate);

    if (context.shouldCancel())
        return juce::Result::fail ("Cancelled");

    // Demucs needs at least a little audio to work on: pad very short files.
    const int originalEngineLength = input.getNumSamples();
    const int minLength = (int) engineSampleRate;   // 1 s

    if (input.getNumSamples() < minLength)
        input.setSize (2, minLength, true, true, false);

    // 2. Separate
    auto separator = createSeparator (eng);
    std::vector<StemAudio> stems;

    SeparationContext inner = context;
    inner.reportProgress = mapped (0.03f, 0.93f);

    auto r = separator->separate (input, stems, inner);

    if (r.failed())
        return r;

    // 3. Back to the source's sample rate and length
    report (0.96f, "Finishing");
    const int length = src.getNumSamples();

    for (auto& stem : stems)
    {
        if (context.shouldCancel())
            return juce::Result::fail ("Cancelled");

        if (stem.buffer.getNumSamples() > originalEngineLength)
            stem.buffer.setSize (stem.buffer.getNumChannels(), originalEngineLength, true, false, false);

        if (std::abs (src.sampleRate - engineSampleRate) > 0.5)
            stem.buffer = AudioIO::resample (stem.buffer, engineSampleRate, src.sampleRate);

        if (stem.buffer.getNumSamples() != length)
            stem.buffer.setSize (stem.buffer.getNumChannels(), length, true, true, false);
    }

    result.engine = eng;
    result.peaks.clear();

    for (auto& stem : stems)
        result.peaks.emplace_back (stem.buffer);

    result.stems = std::move (stems);
    result.secondsTaken = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    report (1.0f, "Done");
    return juce::Result::ok();
}

void SeparationJob::run()
{
    auto result = std::make_shared<StemSet>();

    SeparationContext context;
    context.numThreads = getRecommendedThreadCount (engine);
    context.shouldCancel = [this] { return threadShouldExit(); };
    context.reportProgress = [this] (float p, const juce::String& s)
    {
        progress = juce::jmax (progress.load(), juce::jlimit (0.0f, 1.0f, p));

        const juce::ScopedLock sl (stageLock);
        if (stage != s)
            stage = s;
    };

    juce::Result r = juce::Result::ok();

    try
    {
        r = runSynchronously (*source, engine, *result, context);
    }
    catch (const std::bad_alloc&)
    {
        r = juce::Result::fail ("Not enough memory to separate this file.");
    }
    catch (const std::exception& e)
    {
        r = juce::Result::fail (e.what());
    }

    if (threadShouldExit() && r.wasOk())
        r = juce::Result::fail ("Cancelled");

    std::shared_ptr<const StemSet> stems;
    finished = true;

    if (r.wasOk())
        stems = result;

    juce::MessageManager::callAsync ([weakThis = juce::WeakReference<SeparationJob> (this), stems, r]
    {
        if (weakThis != nullptr && weakThis->onFinished)
            weakThis->onFinished (stems, r);
    });
}

} // namespace stemsplitter
