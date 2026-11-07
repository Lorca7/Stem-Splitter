#include "Separator.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <complex>
#include <mutex>
#include <thread>

namespace stemsplitter
{

namespace
{
    /** "Lite" engine: a fast, model-free separation using classic DSP.

        - Harmonic/percussive separation (median filtering, Fitzgerald 2010) gives drums.
        - The harmonic low end becomes bass.
        - Harmonic, centre-panned energy in the voice band becomes vocals.
        - Everything left over is "other".

        The masks always sum to one, so the stems add back up to the original mix exactly. */
    class SpectralSeparator final : public Separator
    {
    public:
        juce::Result separate (const juce::AudioBuffer<float>& input,
                               std::vector<StemAudio>& stems,
                               SeparationContext& context) override
        {
            jassert (input.getNumChannels() == 2);
            const int total = input.getNumSamples();
            const int numFrames = (total + fftSize - hop) / hop + 1;

            std::vector<juce::AudioBuffer<float>> outputs (numOutputs);

            for (auto& b : outputs)
            {
                b.setSize (2, total);
                b.clear();
            }

            std::vector<float> norm ((size_t) total, 0.0f);

            const int numThreads = juce::jlimit (1, 16, context.numThreads);
            const int framesPerBlock = (numFrames + numThreads - 1) / numThreads;
            std::atomic<int> framesDone { 0 };
            std::atomic<bool> cancelled { false };
            std::mutex mergeLock, progressLock;

            auto work = [&] (int firstFrame, int endFrame)
            {
                try
                {
                    processFrames (input, firstFrame, endFrame, numFrames, outputs, norm, mergeLock, [&]
                    {
                        if (context.shouldCancel())
                            throw SeparationCancelled();

                        const int done = ++framesDone;

                        if ((done & 63) == 0)
                        {
                            const std::lock_guard<std::mutex> lock (progressLock);
                            context.reportProgress ((float) done / (float) numFrames, "Separating");
                        }
                    });
                }
                catch (const SeparationCancelled&)
                {
                    cancelled = true;
                }
            };

            std::vector<std::thread> threads;

            for (int t = 1; t < numThreads; ++t)
            {
                const int first = t * framesPerBlock;
                const int end = juce::jmin (numFrames, first + framesPerBlock);

                if (first < end)
                    threads.emplace_back (work, first, end);
            }

            work (0, juce::jmin (numFrames, framesPerBlock));

            for (auto& t : threads)
                t.join();

            if (cancelled)
                return juce::Result::fail ("Cancelled");

            for (auto& b : outputs)
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* d = b.getWritePointer (ch);

                    for (int i = 0; i < total; ++i)
                        d[i] = norm[(size_t) i] > 1.0e-6f ? d[i] / norm[(size_t) i] : 0.0f;
                }

            stems.clear();
            stems.push_back ({ StemType::vocals, std::move (outputs[vocalsOut]) });
            stems.push_back ({ StemType::drums,  std::move (outputs[drumsOut]) });
            stems.push_back ({ StemType::bass,   std::move (outputs[bassOut]) });
            stems.push_back ({ StemType::other,  std::move (outputs[otherOut]) });
            return juce::Result::ok();
        }

    private:
        static constexpr int fftOrder = 12;
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int hop = fftSize / 4;
        static constexpr int numBins = fftSize / 2 + 1;
        static constexpr int medianSpan = 17;    // frames (time) and bins (frequency)
        static constexpr int halfSpan = medianSpan / 2;
        static constexpr int numOutputs = 4;
        enum { vocalsOut = 0, drumsOut, bassOut, otherOut };

        using Complex = std::complex<float>;

        struct Frame
        {
            std::vector<Complex> left, right;
            std::vector<float> magnitude;
        };

        static int framePosition (int frame) noexcept { return frame * hop - (fftSize - hop); }

        static float smoothStep (float edge0, float edge1, float x) noexcept
        {
            const float t = juce::jlimit (0.0f, 1.0f, (x - edge0) / (edge1 - edge0));
            return t * t * (3.0f - 2.0f * t);
        }

        void processFrames (const juce::AudioBuffer<float>& input, int firstFrame, int endFrame, int numFrames,
                            std::vector<juce::AudioBuffer<float>>& outputs, std::vector<float>& norm,
                            std::mutex& mergeLock, const std::function<void()>& tick) const
        {
            const int total = input.getNumSamples();
            juce::dsp::FFT fft (fftOrder);

            std::vector<float> window ((size_t) fftSize);
            for (int i = 0; i < fftSize; ++i)   // sqrt periodic Hann: analysis * synthesis = Hann
                window[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize));

            // Per-bin weights
            std::vector<float> bassWeight ((size_t) numBins), voiceWeight ((size_t) numBins);

            for (int k = 0; k < numBins; ++k)
            {
                const float f = (float) k * (float) engineSampleRate / (float) fftSize;
                bassWeight[(size_t) k] = 1.0f - smoothStep (120.0f, 260.0f, f);
                voiceWeight[(size_t) k] = smoothStep (150.0f, 300.0f, f) * (1.0f - 0.7f * smoothStep (7000.0f, 14000.0f, f));
            }

            // Local output accumulators covering this block's samples
            const int blockStart = juce::jmax (0, framePosition (firstFrame));
            const int blockEnd = juce::jmin (total, framePosition (endFrame - 1) + fftSize);
            const int blockLen = juce::jmax (0, blockEnd - blockStart);

            std::vector<juce::AudioBuffer<float>> local (numOutputs);
            for (auto& b : local)
            {
                b.setSize (2, blockLen);
                b.clear();
            }
            std::vector<float> localNorm ((size_t) blockLen, 0.0f);

            // Ring of analysed frames for the time-direction median
            std::vector<Frame> ring (medianSpan);
            std::vector<int> ringFrame (medianSpan, -1);

            for (auto& fr : ring)
            {
                fr.left.resize ((size_t) numBins);
                fr.right.resize ((size_t) numBins);
                fr.magnitude.resize ((size_t) numBins);
            }

            std::vector<float> fftBuffer ((size_t) fftSize * 2);

            auto analyse = [&] (int frame)
            {
                auto& fr = ring[(size_t) (frame % medianSpan)];
                ringFrame[(size_t) (frame % medianSpan)] = frame;
                const int pos = framePosition (frame);

                for (int ch = 0; ch < 2; ++ch)
                {
                    std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
                    const float* src = input.getReadPointer (ch);

                    for (int i = 0; i < fftSize; ++i)
                    {
                        const int t = pos + i;
                        if (t >= 0 && t < total)
                            fftBuffer[(size_t) i] = src[t] * window[(size_t) i];
                    }

                    fft.performRealOnlyForwardTransform (fftBuffer.data(), true);
                    auto& dest = ch == 0 ? fr.left : fr.right;

                    for (int k = 0; k < numBins; ++k)
                        dest[(size_t) k] = { fftBuffer[(size_t) (2 * k)], fftBuffer[(size_t) (2 * k + 1)] };
                }

                for (int k = 0; k < numBins; ++k)
                    fr.magnitude[(size_t) k] = 0.5f * std::abs (fr.left[(size_t) k] + fr.right[(size_t) k])
                                             + 0.25f * std::abs (fr.left[(size_t) k] - fr.right[(size_t) k]);
            };

            std::vector<float> harmonic ((size_t) numBins), percussive ((size_t) numBins), scratch (medianSpan);
            std::array<std::vector<float>, numOutputs> masks;
            for (auto& m : masks)
                m.resize ((size_t) numBins);

            const int analyseStart = juce::jmax (0, firstFrame - halfSpan);
            int nextToAnalyse = analyseStart;

            for (int c = firstFrame; c < endFrame; ++c)
            {
                const int needUpTo = juce::jmin (numFrames - 1, c + halfSpan);

                while (nextToAnalyse <= needUpTo)
                    analyse (nextToAnalyse++);

                const auto& centre = ring[(size_t) (c % medianSpan)];
                jassert (ringFrame[(size_t) (c % medianSpan)] == c);

                // Harmonic: median across time. Percussive: median across frequency.
                const int t0 = juce::jmax (0, c - halfSpan), t1 = juce::jmin (numFrames - 1, c + halfSpan);

                for (int k = 0; k < numBins; ++k)
                {
                    int n = 0;
                    for (int t = t0; t <= t1; ++t)
                        scratch[(size_t) n++] = ring[(size_t) (t % medianSpan)].magnitude[(size_t) k];

                    std::nth_element (scratch.begin(), scratch.begin() + n / 2, scratch.begin() + n);
                    harmonic[(size_t) k] = scratch[(size_t) (n / 2)];

                    const int k0 = juce::jmax (0, k - halfSpan), k1 = juce::jmin (numBins - 1, k + halfSpan);
                    n = 0;
                    for (int j = k0; j <= k1; ++j)
                        scratch[(size_t) n++] = centre.magnitude[(size_t) j];

                    std::nth_element (scratch.begin(), scratch.begin() + n / 2, scratch.begin() + n);
                    percussive[(size_t) k] = scratch[(size_t) (n / 2)];
                }

                for (int k = 0; k < numBins; ++k)
                {
                    const float h2 = harmonic[(size_t) k] * harmonic[(size_t) k];
                    const float p2 = percussive[(size_t) k] * percussive[(size_t) k];
                    const float mh = h2 / (h2 + p2 + 1.0e-12f);
                    const float mp = 1.0f - mh;

                    const auto l = centre.left[(size_t) k], r = centre.right[(size_t) k];
                    const float energy = std::norm (l) + std::norm (r);
                    const float similarity = energy > 1.0e-12f ? 2.0f * std::abs (l * std::conj (r)) / energy : 1.0f;
                    const float centredness = std::pow (juce::jlimit (0.0f, 1.0f, similarity), 6.0f);

                    const float bass = mh * bassWeight[(size_t) k];
                    const float vocals = mh * (1.0f - bassWeight[(size_t) k]) * voiceWeight[(size_t) k] * centredness * 0.9f;
                    const float drums = mp * (1.0f - 0.5f * bassWeight[(size_t) k]);
                    const float other = juce::jmax (0.0f, 1.0f - bass - vocals - drums);

                    masks[vocalsOut][(size_t) k] = vocals;
                    masks[drumsOut][(size_t) k] = drums;
                    masks[bassOut][(size_t) k] = bass;
                    masks[otherOut][(size_t) k] = other;
                }

                // Resynthesise each stem
                const int pos = framePosition (c);

                for (int s = 0; s < numOutputs; ++s)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const auto& spec = ch == 0 ? centre.left : centre.right;

                        for (int k = 0; k < numBins; ++k)
                        {
                            const auto v = spec[(size_t) k] * masks[(size_t) s][(size_t) k];
                            fftBuffer[(size_t) (2 * k)] = v.real();
                            fftBuffer[(size_t) (2 * k + 1)] = v.imag();
                        }

                        for (int k = numBins; k < fftSize; ++k)   // Hermitian mirror
                        {
                            const auto v = spec[(size_t) (fftSize - k)] * masks[(size_t) s][(size_t) (fftSize - k)];
                            fftBuffer[(size_t) (2 * k)] = v.real();
                            fftBuffer[(size_t) (2 * k + 1)] = -v.imag();
                        }

                        fft.performRealOnlyInverseTransform (fftBuffer.data());
                        float* dst = local[(size_t) s].getWritePointer (ch);

                        for (int i = 0; i < fftSize; ++i)
                        {
                            const int t = pos + i - blockStart;
                            if (t >= 0 && t < blockLen && pos + i < total)
                                dst[t] += fftBuffer[(size_t) i] * window[(size_t) i];
                        }
                    }
                }

                for (int i = 0; i < fftSize; ++i)
                {
                    const int t = pos + i - blockStart;
                    if (t >= 0 && t < blockLen && pos + i < total)
                        localNorm[(size_t) t] += window[(size_t) i] * window[(size_t) i];
                }

                tick();
            }

            const std::lock_guard<std::mutex> lock (mergeLock);

            for (int s = 0; s < numOutputs; ++s)
                for (int ch = 0; ch < 2; ++ch)
                    outputs[(size_t) s].addFrom (ch, blockStart, local[(size_t) s], ch, 0, blockLen);

            for (int i = 0; i < blockLen; ++i)
                norm[(size_t) (blockStart + i)] += localNorm[(size_t) i];
        }
    };
}

std::unique_ptr<Separator> createSpectralSeparator()
{
    return std::make_unique<SpectralSeparator>();
}

} // namespace stemsplitter
