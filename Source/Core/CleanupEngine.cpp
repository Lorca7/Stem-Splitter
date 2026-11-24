#include "CleanupEngine.h"

#include <thread>

namespace stemsplitter
{

namespace
{
    bool sameIgnoringEnabled (CleanupSettings a, CleanupSettings b)
    {
        a.enabled = b.enabled = true;
        return a == b;
    }
}

CleanupEngine::CleanupEngine() : juce::Thread ("Stem Splitter clean-up")
{
    startThread (juce::Thread::Priority::normal);
}

CleanupEngine::~CleanupEngine()
{
    masterReference.clear();
    stopTimer();
    signalThreadShouldExit();
    generation++;
    wakeUp.signal();
    stopThread (20000);
}

void CleanupEngine::setInputs (std::shared_ptr<const SourceAudio> newSource, std::shared_ptr<const StemSet> newStems)
{
    if (newSource == source && newStems == stems)
        return;

    if (newSource != source)
    {
        analysis.reset();
        analysedSource.reset();
        analysisRequested.reset();
        result.reset();          // frees the old cleaned audio
        customProfile = {};      // a learned profile belongs to the old recording
        profileVersion++;
    }

    source = std::move (newSource);
    stems = std::move (newStems);
    schedule (0);
}

void CleanupEngine::setSettings (const CleanupSettings& newSettings)
{
    const bool renderRelevantChange = ! sameIgnoringEnabled (newSettings, settings);
    settings = newSettings;

    if (renderRelevantChange || (settings.enabled && ! resultMatches (settings)))
        schedule (250);   // debounce slider drags
}

void CleanupEngine::setCustomProfile (const NoiseProfile& p)
{
    customProfile = p;
    profileVersion++;
    schedule (0);
}

void CleanupEngine::clearCustomProfile()
{
    if (! customProfile.isValid())
        return;

    customProfile = {};
    profileVersion++;
    schedule (0);
}

bool CleanupEngine::resultMatches (const CleanupSettings& s) const
{
    return result != nullptr && result->inputSource == source && result->inputStems == stems
        && result->profileVersion == profileVersion && sameIgnoringEnabled (result->settings, s);
}

void CleanupEngine::schedule (int delayMs)
{
    if (delayMs <= 0)
        timerCallback();
    else
        startTimer (delayMs);
}

void CleanupEngine::timerCallback()
{
    stopTimer();

    const bool needsAnalysis = source != nullptr && analysedSource != source && analysisRequested != source;
    const bool needsRender = source != nullptr && settings.enabled && settings.hasAnyEffect() && ! resultMatches (settings);

    if (! needsAnalysis && ! needsRender)
        return;

    Job job;
    job.source = source;
    job.stems = stems;
    job.settings = settings;
    job.settings.enabled = true;
    job.customProfile = customProfile;
    job.profileVersion = profileVersion;
    job.analysis = analysedSource == source ? analysis : nullptr;
    job.needsAnalysis = needsAnalysis;
    job.render = needsRender;
    job.generation = ++generation;   // cancels whatever is running

    if (needsAnalysis)
        analysisRequested = source;

    {
        const juce::ScopedLock sl (jobLock);

        // Don't lose an analysis that was queued but not yet started.
        if (pendingJob.has_value() && pendingJob->needsAnalysis && pendingJob->source == job.source)
            job.needsAnalysis = true;

        pendingJob = std::move (job);
    }

    if (needsRender)
        rendering = true;

    wakeUp.signal();
}

void CleanupEngine::run()
{
    while (! threadShouldExit())
    {
        wakeUp.wait (-1);

        std::optional<Job> next;

        {
            const juce::ScopedLock sl (jobLock);
            std::swap (next, pendingJob);
        }

        if (! next.has_value() || threadShouldExit())
            continue;

        auto job = std::move (*next);
        const auto myGeneration = job.generation;
        auto cancelled = [this, myGeneration] { return threadShouldExit() || generation.load() != myGeneration; };

        // 1. Analysis (once per song; only cancelled by quitting)
        if (job.needsAnalysis)
        {
            analysing = true;
            auto a = std::make_shared<CleanupAnalysis> (
                NoiseCleanup::analyse (job.source->buffer, job.source->sampleRate, [this] { return threadShouldExit(); }));
            analysing = false;

            if (threadShouldExit())
                return;

            job.analysis = a;

            juce::MessageManager::callAsync ([weakThis = juce::WeakReference<CleanupEngine> (this), a, src = job.source]
            {
                if (auto* self = weakThis.get())
                {
                    if (self->source != src)
                        return;

                    self->analysis = a;
                    self->analysedSource = src;

                    if (self->onChange)
                        self->onChange();

                    // Render now if the clean-up was switched on while we were analysing.
                    self->schedule (0);
                }
            });

            if (! job.render)
                continue;
        }

        if (! job.render || job.analysis == nullptr || cancelled())
        {
            if (generation.load() == myGeneration)
                rendering = false;
            continue;
        }

        // 2. Render the original and every stem in parallel
        progress = 0.0f;
        const auto& profile = job.customProfile.isValid() ? job.customProfile : job.analysis->autoProfile;
        const float humHz = NoiseCleanup::resolveHumHz (job.settings, *job.analysis);
        const double sr = job.source->sampleRate;

        std::vector<const juce::AudioBuffer<float>*> inputs { &job.source->buffer };
        if (job.stems != nullptr)
            for (const auto& s : job.stems->stems)
                inputs.push_back (&s.buffer);

        std::vector<juce::AudioBuffer<float>> outputs (inputs.size());
        std::vector<std::atomic<float>> partial (inputs.size());
        for (auto& p : partial)
            p = 0.0f;

        std::atomic<size_t> nextIndex { 0 };

        auto worker = [&]
        {
            for (;;)
            {
                const size_t i = nextIndex.fetch_add (1);
                if (i >= inputs.size() || cancelled())
                    return;

                outputs[i] = NoiseCleanup::process (*inputs[i], sr, job.settings, profile, humHz, cancelled,
                                                    [&partial, &inputs, this, i] (float p)
                                                    {
                                                        partial[i] = p;
                                                        float sum = 0.0f;
                                                        for (auto& v : partial)
                                                            sum += v.load();
                                                        progress = sum / (float) inputs.size();
                                                    });
            }
        };

        const int numWorkers = juce::jlimit (1, (int) inputs.size(), juce::SystemStats::getNumCpus());
        std::vector<std::thread> threads;
        for (int i = 1; i < numWorkers; ++i)
            threads.emplace_back (worker);
        worker();
        for (auto& t : threads)
            t.join();

        if (cancelled())
            continue;   // a newer job is already queued (it owns the "rendering" flag)

        auto r = std::make_shared<Result>();
        r->inputSource = job.source;
        r->inputStems = job.stems;
        r->settings = job.settings;
        r->profileVersion = job.profileVersion;
        r->humHz = humHz;

        auto cleanSource = std::make_shared<SourceAudio>();
        cleanSource->file = job.source->file;
        cleanSource->sampleRate = job.source->sampleRate;
        cleanSource->originalNumChannels = job.source->originalNumChannels;
        cleanSource->bitsPerSample = job.source->bitsPerSample;
        cleanSource->formatName = job.source->formatName;
        cleanSource->buffer = std::move (outputs[0]);
        r->sourcePeaks = WaveformPeaks (cleanSource->buffer);
        r->source = cleanSource;

        if (job.stems != nullptr)
        {
            auto cleanStems = std::make_shared<StemSet>();
            cleanStems->engine = job.stems->engine;
            cleanStems->secondsTaken = job.stems->secondsTaken;

            for (size_t i = 0; i < job.stems->stems.size(); ++i)
            {
                cleanStems->stems.push_back ({ job.stems->stems[i].type, std::move (outputs[i + 1]) });
                cleanStems->peaks.emplace_back (cleanStems->stems.back().buffer);
            }

            r->stems = cleanStems;
        }

        juce::MessageManager::callAsync ([weakThis = juce::WeakReference<CleanupEngine> (this), r, myGeneration]
        {
            if (auto* self = weakThis.get())
            {
                self->result = r;

                if (self->generation.load() == myGeneration)
                    self->rendering = false;

                if (self->onChange)
                    self->onChange();
            }
        });
    }
}

} // namespace stemsplitter
