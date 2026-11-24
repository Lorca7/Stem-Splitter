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
    /** Runs one Demucs model over the whole input.

        This mirrors demucs.cpp's own split_inference (7.8 s segments, 25% overlap, triangular
        cross-fades) but processes the segments in parallel, one worker per core, each with its own
        working buffers. Compared with splitting the song into chunks and running the full pipeline on
        each, this avoids redundant context and padding: the network only sees ~1.33x the song length.

        Returns one stereo buffer per model output. */
    std::vector<juce::AudioBuffer<float>> runModelThreaded (const demucscpp::demucs_model& model,
                                                            const juce::AudioBuffer<float>& input,
                                                            SeparationContext& context,
                                                            float progressStart, float progressSpan,
                                                            const juce::String& stageName)
    {
        const int total = input.getNumSamples();
        const int numSources = model.is_4sources ? 4 : 6;
        const int segment = (int) (demucscpp::SEGMENT_LEN_SECS * demucscpp::SUPPORTED_SAMPLE_RATE);
        const int stride = (int) ((1.0f - demucscpp::OVERLAP) * (float) segment);

        std::vector<juce::AudioBuffer<float>> outputs;

        for (int s = 0; s < numSources; ++s)
        {
            outputs.emplace_back (2, total);
            outputs.back().clear();
        }

        // Normalise like Demucs: by the mean and standard deviation of the mono mix, over the whole song.
        double sum = 0.0, sumSq = 0.0;
        const float* l = input.getReadPointer (0);
        const float* r = input.getReadPointer (1);

        for (int i = 0; i < total; ++i)
        {
            const double m = 0.5 * ((double) l[i] + (double) r[i]);
            sum += m;
            sumSq += m * m;
        }

        const double mean = sum / juce::jmax (1, total);
        const double variance = total > 1 ? (sumSq - (double) total * mean * mean) / (double) (total - 1) : 0.0;
        const float refMean = (float) mean;
        const float refStd = (float) std::sqrt (juce::jmax (0.0, variance));

        if (refStd < 1.0e-7f)   // digital silence separates into silence
        {
            context.reportProgress (progressStart + progressSpan, stageName);
            return outputs;
        }

        // Triangular transition window, as demucs.cpp (TRANSITION_POWER = 1)
        std::vector<float> weight ((size_t) segment, 0.0f);

        for (int i = 0; i < segment / 2; ++i)
        {
            weight[(size_t) i] = (float) (i + 1) / (float) (segment / 2);
            weight[(size_t) (segment - 1 - i)] = weight[(size_t) i];
        }

        std::vector<int> offsets;
        for (int offset = 0; offset < total; offset += stride)
            offsets.push_back (offset);

        std::vector<float> sumWeight ((size_t) total, 0.0f);
        std::vector<std::atomic<float>> segmentProgress (offsets.size());
        for (auto& p : segmentProgress)
            p = 0.0f;

        std::atomic<size_t> next { 0 };
        std::atomic<bool> stop { false };
        std::mutex accumulateLock, progressLock;
        juce::String errorMessage;
        bool cancelled = false;

        auto reportOverall = [&]
        {
            double done = 0.0;
            for (auto& p : segmentProgress)
                done += p.load();

            const std::lock_guard<std::mutex> lock (progressLock);
            context.reportProgress (progressStart + progressSpan * (float) (done / (double) offsets.size()), stageName);
        };

        auto worker = [&]
        {
            std::unique_ptr<demucscpp::demucs_segment_buffers> buffers;
            std::unique_ptr<demucscpp::stft_buffers> stft;

            try
            {
                buffers = std::make_unique<demucscpp::demucs_segment_buffers> (2, segment, numSources);
                stft = std::make_unique<demucscpp::stft_buffers> (buffers->padded_segment_samples);
            }
            catch (const std::bad_alloc&)
            {
                stop = true;
                const std::lock_guard<std::mutex> lock (accumulateLock);
                errorMessage = "Not enough memory to separate this file. Close other apps and try again.";
                return;
            }

            for (;;)
            {
                const size_t index = next.fetch_add (1);

                if (index >= offsets.size() || stop.load())
                    return;

                const int offset = offsets[index];
                const int length = juce::jmin (segment, total - offset);
                const int leftPad = (segment - length) / 2;   // symmetric zero padding, as demucs.cpp

                buffers->mix.setZero();

                for (int i = 0; i < length; ++i)
                {
                    buffers->mix (0, leftPad + i) = (l[offset + i] - refMean) / refStd;
                    buffers->mix (1, leftPad + i) = (r[offset + i] - refMean) / refStd;
                }

                try
                {
                    demucscpp::ProgressCallback cb = [&, index] (float p, const std::string&)
                    {
                        if (stop.load() || context.shouldCancel())
                            throw SeparationCancelled();

                        segmentProgress[index] = juce::jlimit (0.0f, 1.0f, p);
                        reportOverall();
                    };

                    demucscpp::model_inference (model, *buffers, *stft, cb, 0.0f, 1.0f);
                }
                catch (const SeparationCancelled&)
                {
                    stop = true;
                    const std::lock_guard<std::mutex> lock (accumulateLock);
                    cancelled = true;
                    return;
                }
                catch (const std::exception& e)
                {
                    stop = true;
                    const std::lock_guard<std::mutex> lock (accumulateLock);
                    errorMessage = "The separation engine failed: " + juce::String (e.what());
                    return;
                }

                {
                    const std::lock_guard<std::mutex> lock (accumulateLock);

                    for (int s = 0; s < numSources; ++s)
                    {
                        for (int ch = 0; ch < 2; ++ch)
                        {
                            float* dst = outputs[(size_t) s].getWritePointer (ch, offset);

                            for (int i = 0; i < length; ++i)
                            {
                                const float v = buffers->targets_out (s, ch, leftPad + i);
                                dst[i] += std::isfinite (v) ? weight[(size_t) i] * v : 0.0f;
                            }
                        }
                    }

                    for (int i = 0; i < length; ++i)
                        sumWeight[(size_t) (offset + i)] += weight[(size_t) i];
                }

                segmentProgress[index] = 1.0f;
                reportOverall();
            }
        };

        const int numWorkers = juce::jlimit (1, juce::jmax (1, (int) offsets.size()), context.numThreads);
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

        // Undo the weighting and the normalisation
        for (auto& out : outputs)
            for (int ch = 0; ch < 2; ++ch)
            {
                float* d = out.getWritePointer (ch);

                for (int i = 0; i < total; ++i)
                    d[i] = sumWeight[(size_t) i] > 0.0f ? d[i] / sumWeight[(size_t) i] * refStd + refMean : 0.0f;
            }

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
