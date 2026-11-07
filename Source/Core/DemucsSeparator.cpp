// demucs.cpp (Eigen) headers first, before any JUCE headers.
#include "model.hpp"
#include "tensor.hpp"

#include "Separator.h"
#include "ModelManager.h"

#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>

namespace stemsplitter
{

namespace
{
    /** Demucs output index → stem. Order: drums, bass, other, vocals, (guitar, piano). */
    StemType stemForDemucsIndex (int index)
    {
        switch (index)
        {
            case 0:  return StemType::drums;
            case 1:  return StemType::bass;
            case 2:  return StemType::other;
            case 3:  return StemType::vocals;
            case 4:  return StemType::guitar;
            case 5:  return StemType::piano;
            default: return StemType::other;
        }
    }

    int demucsIndexForStem (StemType t)
    {
        for (int i = 0; i < 6; ++i)
            if (stemForDemucsIndex (i) == t)
                return i;

        return 2;
    }

    //==========================================================================
    /** Keeps the most recently used model in memory so repeated separations start instantly. */
    class ModelCache
    {
    public:
        static ModelCache& get()
        {
            static ModelCache instance;
            return instance;
        }

        std::shared_ptr<const demucscpp::demucs_model> load (const juce::File& file, juce::String& error)
        {
            const std::lock_guard<std::mutex> lock (mutex);

            if (cached != nullptr && cachedFile == file && cachedTime == file.getLastModificationTime())
                return cached;

            cached.reset();   // free the old model before loading a new one

            auto model = std::make_shared<demucscpp::demucs_model>();

            if (! demucscpp::load_demucs_model (file.getFullPathName().toStdString(), model.get()))
            {
                error = "The model file " + file.getFileName() + " couldn't be loaded. It may be damaged; "
                        "delete it and download it again.";
                return nullptr;
            }

            cached = model;
            cachedFile = file;
            cachedTime = file.getLastModificationTime();
            return cached;
        }

        void clear()
        {
            const std::lock_guard<std::mutex> lock (mutex);
            cached.reset();
        }

    private:
        std::mutex mutex;
        std::shared_ptr<const demucscpp::demucs_model> cached;
        juce::File cachedFile;
        juce::Time cachedTime;
    };

    //==========================================================================
    struct Chunk
    {
        int coreStart = 0, coreEnd = 0;   // region this chunk is responsible for
        int segStart = 0, segEnd = 0;     // region actually processed (core + context)
        bool hasPrev = false, hasNext = false;
    };

    /** Runs one Demucs model over the whole input, split into overlapping chunks that are
        processed in parallel and cross-faded back together.

        Returns a tensor-like set of buffers: one stereo buffer per model output. */
    std::vector<juce::AudioBuffer<float>> runModelThreaded (const demucscpp::demucs_model& model,
                                                            const juce::AudioBuffer<float>& input,
                                                            SeparationContext& context,
                                                            float progressStart, float progressSpan,
                                                            const juce::String& stageName)
    {
        const int total = input.getNumSamples();
        const int numSources = model.is_4sources ? 4 : 6;
        const int sr = (int) engineSampleRate;

        // Split into chunks: enough to keep every thread busy, but no longer than 30 s each so
        // memory stays bounded and progress is smooth. Each chunk gets 2 s of context per side.
        const int numThreads = juce::jmax (1, context.numThreads);
        const int contextLen = 2 * sr;
        const int minCore = 8 * sr, maxCore = 30 * sr;
        const int coreLen = juce::jlimit (minCore, maxCore, (total + numThreads - 1) / numThreads);

        std::vector<Chunk> chunks;

        for (int start = 0; start < total; start += coreLen)
        {
            Chunk c;
            c.coreStart = start;
            c.coreEnd = juce::jmin (total, start + coreLen);
            c.hasPrev = start > 0;
            c.hasNext = c.coreEnd < total;
            c.segStart = c.hasPrev ? juce::jmax (0, c.coreStart - contextLen) : 0;
            c.segEnd = c.hasNext ? juce::jmin (total, c.coreEnd + contextLen) : total;
            chunks.push_back (c);
        }

        // Merge a very short tail into its predecessor (Demucs needs some audio to work with).
        if (chunks.size() > 1 && chunks.back().coreEnd - chunks.back().coreStart < 2 * sr)
        {
            chunks[chunks.size() - 2].coreEnd = total;
            chunks[chunks.size() - 2].segEnd = total;
            chunks[chunks.size() - 2].hasNext = false;
            chunks.pop_back();
        }

        std::vector<juce::AudioBuffer<float>> outputs;

        for (int s = 0; s < numSources; ++s)
        {
            outputs.emplace_back (2, total);
            outputs.back().clear();
        }

        std::vector<std::atomic<float>> chunkProgress (chunks.size());
        for (auto& p : chunkProgress)
            p = 0.0f;

        std::atomic<size_t> nextChunk { 0 };
        std::atomic<bool> failed { false };
        std::mutex accumulateLock, progressLock;
        juce::String errorMessage;
        bool cancelled = false;

        auto reportOverall = [&]
        {
            double done = 0.0;

            for (size_t i = 0; i < chunks.size(); ++i)
                done += (double) chunkProgress[i].load() * (chunks[i].coreEnd - chunks[i].coreStart);

            const std::lock_guard<std::mutex> lock (progressLock);
            context.reportProgress (progressStart + progressSpan * (float) (done / juce::jmax (1, total)), stageName);
        };

        auto worker = [&]
        {
            for (;;)
            {
                const size_t index = nextChunk.fetch_add (1);

                if (index >= chunks.size() || failed.load())
                    return;

                const auto& c = chunks[index];
                const int segLen = c.segEnd - c.segStart;

                Eigen::MatrixXf segment (2, segLen);
                float peak = 0.0f;

                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* src = input.getReadPointer (ch, c.segStart);

                    for (int i = 0; i < segLen; ++i)
                    {
                        segment (ch, i) = src[i];
                        peak = juce::jmax (peak, std::abs (src[i]));
                    }
                }

                // Demucs normalises by the signal's standard deviation, so pure digital silence
                // would produce NaNs. Silence separates into silence anyway.
                if (peak < 1.0e-6f)
                {
                    chunkProgress[index] = 1.0f;
                    reportOverall();
                    continue;
                }

                Eigen::Tensor3dXf result;

                try
                {
                    demucscpp::ProgressCallback cb = [&, index] (float p, const std::string&)
                    {
                        if (context.shouldCancel() || failed.load())
                            throw SeparationCancelled();

                        chunkProgress[index] = juce::jlimit (0.0f, 1.0f, p);
                        reportOverall();
                    };

                    result = demucscpp::demucs_inference (model, segment, cb);
                }
                catch (const SeparationCancelled&)
                {
                    failed = true;
                    const std::lock_guard<std::mutex> lock (accumulateLock);
                    cancelled = true;
                    return;
                }
                catch (const std::exception& e)
                {
                    failed = true;
                    const std::lock_guard<std::mutex> lock (accumulateLock);
                    errorMessage = "The separation engine failed: " + juce::String (e.what());
                    return;
                }

                if (result.dimension (0) < numSources || result.dimension (2) < segLen)
                {
                    failed = true;
                    const std::lock_guard<std::mutex> lock (accumulateLock);
                    errorMessage = "The separation engine returned unexpected output.";
                    return;
                }

                // Linear cross-fade across the shared context regions: weights of neighbouring
                // chunks sum to exactly 1 everywhere.
                const float fadeLen = (float) (2 * contextLen);
                std::vector<float> weights ((size_t) segLen, 1.0f);

                for (int i = 0; i < segLen; ++i)
                {
                    const int t = c.segStart + i;
                    float w = 1.0f;

                    if (c.hasPrev)
                        w = juce::jmin (w, juce::jlimit (0.0f, 1.0f, (float) (t - (c.coreStart - contextLen)) / fadeLen));

                    if (c.hasNext)
                        w = juce::jmin (w, juce::jlimit (0.0f, 1.0f, (float) ((c.coreEnd + contextLen) - t) / fadeLen));

                    weights[(size_t) i] = w;
                }

                {
                    const std::lock_guard<std::mutex> lock (accumulateLock);

                    for (int s = 0; s < numSources; ++s)
                    {
                        for (int ch = 0; ch < 2; ++ch)
                        {
                            float* dst = outputs[(size_t) s].getWritePointer (ch, c.segStart);

                            for (int i = 0; i < segLen; ++i)
                            {
                                const float v = result (s, ch, i);
                                dst[i] += std::isfinite (v) ? v * weights[(size_t) i] : 0.0f;
                            }
                        }
                    }
                }

                chunkProgress[index] = 1.0f;
                reportOverall();
            }
        };

        const int numWorkers = juce::jmin (numThreads, (int) chunks.size());
        std::vector<std::thread> threads;

        for (int i = 1; i < numWorkers; ++i)
            threads.emplace_back (worker);

        worker();

        for (auto& t : threads)
            t.join();

        if (cancelled)
            throw SeparationCancelled();

        if (errorMessage.isNotEmpty())
            throw std::runtime_error (errorMessage.toStdString());

        return outputs;
    }

    //==========================================================================
    class DemucsSeparator final : public Separator
    {
    public:
        explicit DemucsSeparator (EngineType e) : engine (e) {}

        juce::Result separate (const juce::AudioBuffer<float>& input,
                               std::vector<StemAudio>& stems,
                               SeparationContext& context) override
        {
            jassert (input.getNumChannels() == 2);
            const auto files = ModelManager::getRequiredFiles (engine);

            for (const auto& name : files)
                if (ModelManager::findModelFile (name) == juce::File())
                    return juce::Result::fail ("The " + getEngineName (engine) + " model isn't installed.");

            try
            {
                if (engine == EngineType::fourStemsHQ)
                    return separateFineTuned (input, stems, context, files);

                juce::String error;
                context.reportProgress (0.0f, "Loading model");
                auto model = ModelCache::get().load (ModelManager::findModelFile (files[0]), error);

                if (model == nullptr)
                    return juce::Result::fail (error);

                auto outputs = runModelThreaded (*model, input, context, 0.0f, 1.0f, "Separating");
                stems.clear();

                for (auto type : getStemsForEngine (engine))
                {
                    const auto index = (size_t) demucsIndexForStem (type);

                    if (index < outputs.size())
                        stems.push_back ({ type, std::move (outputs[index]) });
                }

                return juce::Result::ok();
            }
            catch (const SeparationCancelled&)
            {
                return juce::Result::fail ("Cancelled");
            }
            catch (const std::bad_alloc&)
            {
                ModelCache::get().clear();
                return juce::Result::fail ("Not enough memory to separate this file. Close other apps and try again.");
            }
            catch (const std::exception& e)
            {
                return juce::Result::fail (e.what());
            }
        }

    private:
        /** htdemucs_ft: four specialist networks; each contributes only the stem it was tuned for. */
        juce::Result separateFineTuned (const juce::AudioBuffer<float>& input,
                                        std::vector<StemAudio>& stems,
                                        SeparationContext& context,
                                        const juce::StringArray& files)
        {
            // files are ordered drums, bass, other, vocals = Demucs output indices 0..3
            std::vector<StemAudio> results;

            for (int i = 0; i < files.size(); ++i)
            {
                const auto type = stemForDemucsIndex (i);
                const float start = (float) i / (float) files.size();
                const float span = 1.0f / (float) files.size();
                const auto stage = "Separating " + getStemName (type).toLowerCase();

                context.reportProgress (start, stage);

                juce::String error;
                std::shared_ptr<const demucscpp::demucs_model> model;

                {
                    // Don't keep the specialist models cached: four of them would use >600 MB.
                    ModelCache::get().clear();
                    auto loaded = std::make_shared<demucscpp::demucs_model>();

                    if (! demucscpp::load_demucs_model (ModelManager::findModelFile (files[i]).getFullPathName().toStdString(),
                                                        loaded.get()))
                        return juce::Result::fail ("The model file " + files[i] + " couldn't be loaded.");

                    model = loaded;
                }

                auto outputs = runModelThreaded (*model, input, context, start, span, stage);
                results.push_back ({ type, std::move (outputs[(size_t) i]) });
            }

            stems.clear();

            for (auto type : getStemsForEngine (engine))
                for (auto& r : results)
                    if (r.type == type)
                        stems.push_back (std::move (r));

            return juce::Result::ok();
        }

        EngineType engine;
    };
}

// Defined in SpectralSeparator.cpp
std::unique_ptr<Separator> createSpectralSeparator();

std::unique_ptr<Separator> createSeparator (EngineType engine)
{
    if (engine == EngineType::lite)
        return createSpectralSeparator();

    return std::make_unique<DemucsSeparator> (engine);
}

} // namespace stemsplitter
