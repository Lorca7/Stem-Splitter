#include "NoiseCleanup.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <complex>
#include <numeric>

namespace stemsplitter
{

//==============================================================================
juce::String CleanupSettings::describe (float detectedHumHz) const
{
    juce::StringArray parts;

    if (denoise)
        parts.add ("Denoise " + juce::String (juce::roundToInt (reductionDb)) + " dB");

    if (dehum)
    {
        const int hz = humHz != 0 ? humHz : juce::roundToInt (detectedHumHz);
        parts.add (hz > 0 ? "Hum " + juce::String (hz) + " Hz" : juce::String ("Hum"));
    }

    if (! notchesHz.empty())
        parts.add (juce::String ((int) notchesHz.size()) + (notchesHz.size() == 1 ? " notch" : " notches"));

    if (rumble)
        parts.add ("Rumble " + juce::String (juce::roundToInt (rumbleHz)) + " Hz");

    return parts.joinIntoString (" + ");
}

bool CleanupSettings::operator== (const CleanupSettings& o) const noexcept
{
    auto same = [] (float a, float b) { return juce::exactlyEqual (a, b); };

    if (notchesHz.size() != o.notchesHz.size())
        return false;

    for (size_t i = 0; i < notchesHz.size(); ++i)
        if (! same (notchesHz[i], o.notchesHz[i]))
            return false;

    return enabled == o.enabled && denoise == o.denoise && same (reductionDb, o.reductionDb)
        && same (sensitivity, o.sensitivity) && dehum == o.dehum && humHz == o.humHz
        && humHarmonics == o.humHarmonics && same (notchWidth, o.notchWidth)
        && rumble == o.rumble && same (rumbleHz, o.rumbleHz);
}

float CleanupAnalysis::valueAt (const std::vector<float>& spectrumDb, int fftSize, double sampleRate, float hz)
{
    if (spectrumDb.empty() || fftSize <= 0 || sampleRate <= 0)
        return -120.0f;

    const double pos = (double) hz * fftSize / sampleRate;
    const auto i = (size_t) juce::jlimit (0.0, (double) spectrumDb.size() - 1.0, std::floor (pos));
    const auto j = juce::jmin (i + 1, spectrumDb.size() - 1);
    const float frac = (float) juce::jlimit (0.0, 1.0, pos - (double) i);
    return spectrumDb[i] + frac * (spectrumDb[j] - spectrumDb[i]);
}

//==============================================================================
namespace
{
    int fftOrderFor (int size)
    {
        int order = 0;
        while ((1 << order) < size)
            ++order;
        return order;
    }

    std::vector<float> sqrtHann (int n)
    {
        std::vector<float> w ((size_t) n);
        for (int i = 0; i < n; ++i)
            w[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) n));
        return w;
    }

    std::vector<float> hann (int n)
    {
        std::vector<float> w ((size_t) n);
        for (int i = 0; i < n; ++i)
            w[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) n);
        return w;
    }

    /** Power spectra (mean over channels) of non-overlapping frames, at most maxFrames of them evenly
        spread over the region. Row-major: frame f, bin k at [f * bins + k]. */
    struct FramePowers
    {
        int bins = 0;
        std::vector<float> power;
        std::vector<double> energy;
        int numFrames() const { return bins > 0 ? (int) (power.size() / (size_t) bins) : 0; }
    };

    FramePowers framePowers (const juce::AudioBuffer<float>& audio, int start, int length, int fftSize,
                             const std::vector<float>& window, int maxFrames, const NoiseCleanup::ShouldCancel& cancel)
    {
        FramePowers fp;
        fp.bins = fftSize / 2 + 1;

        const int available = juce::jmax (1, (length + fftSize - 1) / fftSize);
        const int frames = juce::jmin (available, maxFrames);
        const double stride = (double) available / (double) frames;

        fp.power.assign ((size_t) frames * (size_t) fp.bins, 0.0f);
        fp.energy.assign ((size_t) frames, 0.0);

        juce::dsp::FFT fft (fftOrderFor (fftSize));
        std::vector<float> buf ((size_t) fftSize * 2);
        const int numCh = audio.getNumChannels();

        for (int f = 0; f < frames; ++f)
        {
            if (cancel && (f & 63) == 0 && cancel())
                return {};

            const int pos = start + (int) (f * stride) * fftSize;
            float* dst = fp.power.data() + (size_t) f * (size_t) fp.bins;

            for (int ch = 0; ch < numCh; ++ch)
            {
                std::fill (buf.begin(), buf.end(), 0.0f);
                const float* src = audio.getReadPointer (ch);

                for (int i = 0; i < fftSize; ++i)
                {
                    const int t = pos + i;
                    if (t < start + length && t < audio.getNumSamples())
                        buf[(size_t) i] = src[t] * window[(size_t) i];
                }

                fft.performRealOnlyForwardTransform (buf.data(), true);

                for (int k = 0; k < fp.bins; ++k)
                {
                    const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)];
                    dst[k] += (re * re + im * im) / (float) numCh;
                }
            }

            double e = 0.0;
            for (int k = 0; k < fp.bins; ++k)
                e += dst[k];

            fp.energy[(size_t) f] = e;
        }

        return fp;
    }

    /** Per-bin percentile over the given frames. */
    std::vector<float> percentile (const FramePowers& fp, const std::vector<int>& frames, double q)
    {
        std::vector<float> result ((size_t) fp.bins, 0.0f);

        if (frames.empty())
            return result;

        std::vector<float> column (frames.size());
        const auto nth = (size_t) juce::jlimit (0.0, (double) frames.size() - 1.0, q * (double) (frames.size() - 1));

        for (int k = 0; k < fp.bins; ++k)
        {
            for (size_t i = 0; i < frames.size(); ++i)
                column[i] = fp.power[(size_t) frames[i] * (size_t) fp.bins + (size_t) k];

            std::nth_element (column.begin(), column.begin() + (long) nth, column.end());
            result[(size_t) k] = column[nth];
        }

        return result;
    }

    std::vector<int> nonSilentFrames (const FramePowers& fp)
    {
        std::vector<int> frames;
        for (int f = 0; f < fp.numFrames(); ++f)
            if (fp.energy[(size_t) f] > 1.0e-14)
                frames.push_back (f);
        return frames;
    }

    //==========================================================================
    /** Second-order section with double-precision state. */
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

        static Biquad notch (double sr, double f0, double q)
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / sr;
            const double alpha = std::sin (w0) / (2.0 * q);
            const double cosw = std::cos (w0);
            const double a0 = 1.0 + alpha;
            return { 1.0 / a0, -2.0 * cosw / a0, 1.0 / a0, -2.0 * cosw / a0, (1.0 - alpha) / a0 };
        }

        static Biquad highpass (double sr, double f0, double q)
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / sr;
            const double alpha = std::sin (w0) / (2.0 * q);
            const double cosw = std::cos (w0);
            const double a0 = 1.0 + alpha;
            return { (1.0 + cosw) / 2.0 / a0, -(1.0 + cosw) / a0, (1.0 + cosw) / 2.0 / a0, -2.0 * cosw / a0, (1.0 - alpha) / a0 };
        }

        double magnitude (double sr, double hz) const
        {
            const std::complex<double> z = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * hz / sr);
            const auto num = b0 + b1 * z + b2 * z * z;
            const auto den = 1.0 + a1 * z + a2 * z * z;
            return std::abs (num / den);
        }

        void process (float* data, int n) const
        {
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

            for (int i = 0; i < n; ++i)
            {
                const double x = data[i];
                const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1; x1 = x; y2 = y1; y1 = y;
                data[i] = (float) y;
            }
        }
    };

    std::vector<Biquad> designFilters (const CleanupSettings& s, float humHz, double sr)
    {
        std::vector<Biquad> filters;
        const double nyquistLimit = sr * 0.45;

        if (s.dehum && humHz > 0.0f)
        {
            // Constant ~3 Hz bandwidth per harmonic: removes the hum, leaves the music around it.
            for (int n = 1; n <= juce::jlimit (1, 40, s.humHarmonics); ++n)
            {
                const double f = (double) humHz * n;
                if (f >= nyquistLimit)
                    break;
                filters.push_back (Biquad::notch (sr, f, f / 3.0));
            }
        }

        const double q = 60.0 * std::pow (6.0 / 60.0, (double) juce::jlimit (0.0f, 1.0f, s.notchWidth));

        for (auto hz : s.notchesHz)
            if (hz > 10.0f && hz < nyquistLimit)
                filters.push_back (Biquad::notch (sr, hz, q));

        if (s.rumble && s.rumbleHz > 5.0f)
        {
            // 4th-order Butterworth high-pass (run forwards and backwards: 8th order, zero phase)
            filters.push_back (Biquad::highpass (sr, s.rumbleHz, 0.54119610));
            filters.push_back (Biquad::highpass (sr, s.rumbleHz, 1.3065630));
        }

        return filters;
    }

    /** Zero-phase filtering (forward + backward) with reflected padding to avoid edge transients. */
    void filtfilt (const std::vector<Biquad>& filters, float* data, int n, int pad)
    {
        if (filters.empty() || n < 2)
            return;

        pad = juce::jlimit (0, n - 1, pad);
        std::vector<float> ext ((size_t) (n + 2 * pad));

        for (int i = 0; i < pad; ++i)
        {
            ext[(size_t) i] = 2.0f * data[0] - data[pad - i];
            ext[(size_t) (pad + n + i)] = 2.0f * data[n - 1] - data[n - 2 - i];
        }

        std::copy (data, data + n, ext.begin() + pad);

        for (const auto& f : filters)
            f.process (ext.data(), (int) ext.size());

        std::reverse (ext.begin(), ext.end());

        for (const auto& f : filters)
            f.process (ext.data(), (int) ext.size());

        std::reverse (ext.begin(), ext.end());
        std::copy (ext.begin() + pad, ext.begin() + pad + n, data);
    }

    float medianOf (std::vector<float> v)
    {
        if (v.empty())
            return -120.0f;
        std::nth_element (v.begin(), v.begin() + (long) (v.size() / 2), v.end());
        return v[v.size() / 2];
    }

    /** How far a narrow peak near `hz` sticks out of its surroundings (dB). */
    float prominence (const CleanupAnalysis& a, float hz, float peakHalfWidthHz, float neighbourhoodHz)
    {
        const double binHz = a.sampleRate / a.displayFftSize;
        const int centre = (int) std::lround (hz / binHz);
        const int peakBins = juce::jmax (1, (int) std::ceil (peakHalfWidthHz / binHz));
        const int nbBins = juce::jmax (peakBins + 2, (int) std::ceil (neighbourhoodHz / binHz));
        const int last = (int) a.floorDb.size() - 1;

        float peak = -200.0f;
        for (int k = juce::jmax (0, centre - peakBins); k <= juce::jmin (last, centre + peakBins); ++k)
            peak = juce::jmax (peak, a.floorDb[(size_t) k]);

        std::vector<float> around;
        for (int k = juce::jmax (0, centre - nbBins); k <= juce::jmin (last, centre + nbBins); ++k)
            if (std::abs (k - centre) > peakBins + 1)
                around.push_back (a.floorDb[(size_t) k]);

        return peak - medianOf (around);
    }
}

//==============================================================================
namespace NoiseCleanup
{

int getProcessingFftSize (double sampleRate)
{
    return sampleRate > 100000.0 ? 8192 : sampleRate > 50000.0 ? 4096 : 2048;
}

CleanupAnalysis analyse (const juce::AudioBuffer<float>& audio, double sampleRate, ShouldCancel cancel)
{
    CleanupAnalysis a;
    a.sampleRate = sampleRate;
    const int length = audio.getNumSamples();

    if (length == 0 || sampleRate <= 0)
        return a;

    // 1. Noise profile at the processing resolution, with the processing window.
    {
        const int n = getProcessingFftSize (sampleRate);
        const auto fp = framePowers (audio, 0, length, n, sqrtHann (n), 6000, cancel);

        if (fp.numFrames() == 0)
            return {};

        auto frames = nonSilentFrames (fp);
        a.autoProfile.sampleRate = sampleRate;
        a.autoProfile.fftSize = n;
        a.autoProfile.power.assign ((size_t) fp.bins, 0.0f);

        if (! frames.empty())
        {
            // Mean spectrum of the quietest 10% of frames (pauses, intro, outro)...
            std::vector<int> quiet = frames;
            std::sort (quiet.begin(), quiet.end(), [&fp] (int x, int y) { return fp.energy[(size_t) x] < fp.energy[(size_t) y]; });
            quiet.resize (juce::jmax ((size_t) juce::jmin ((int) quiet.size(), 5), quiet.size() / 10));

            std::vector<double> mean ((size_t) fp.bins, 0.0);
            for (int f : quiet)
                for (int k = 0; k < fp.bins; ++k)
                    mean[(size_t) k] += fp.power[(size_t) f * (size_t) fp.bins + (size_t) k];

            // ...capped per bin by what's present nearly all the time, so music in those frames isn't
            // mistaken for noise when a song has no real pauses.
            const auto steady = percentile (fp, frames, 0.1);

            for (int k = 0; k < fp.bins; ++k)
                a.autoProfile.power[(size_t) k] = (float) juce::jmin (mean[(size_t) k] / (double) quiet.size(),
                                                                      2.0 * (double) steady[(size_t) k]);
        }
    }

    if (cancel && cancel())
        return {};

    // 2. Finer spectra (~6 Hz, 0.17 s frames) for display, hum and tone detection. Short frames matter:
    //    the steady floor is taken from the quietest moments, and a long frame would smear notes into
    //    the gaps between them.
    {
        const int n = juce::jlimit (4096, 32768, juce::nextPowerOfTwo ((int) (sampleRate / 6.0)));
        const auto window = hann (n);
        const auto fp = framePowers (audio, 0, length, n, window, 3000, cancel);

        if (fp.numFrames() == 0)
            return {};

        // 0 dB = full-scale sine
        const double windowSum = std::accumulate (window.begin(), window.end(), 0.0);
        const double ref = (windowSum / 2.0) * (windowSum / 2.0);

        auto frames = nonSilentFrames (fp);
        if (frames.empty())
            for (int f = 0; f < fp.numFrames(); ++f)
                frames.push_back (f);

        std::vector<double> mean ((size_t) fp.bins, 0.0);
        for (int f : frames)
            for (int k = 0; k < fp.bins; ++k)
                mean[(size_t) k] += fp.power[(size_t) f * (size_t) fp.bins + (size_t) k];

        const auto floor = percentile (fp, frames, 0.1);
        a.displayFftSize = n;
        a.averageDb.resize ((size_t) fp.bins);
        a.floorDb.resize ((size_t) fp.bins);

        for (int k = 0; k < fp.bins; ++k)
        {
            a.averageDb[(size_t) k] = (float) (10.0 * std::log10 (mean[(size_t) k] / (double) frames.size() / ref + 1.0e-14));
            a.floorDb[(size_t) k] = (float) (10.0 * std::log10 ((double) floor[(size_t) k] / ref + 1.0e-14));
        }
    }

    // 3. Mains hum: which of 50/60 Hz has a series of prominent harmonics in the steady floor?
    {
        float bestScore = 0.0f;

        for (float f0 : { 50.0f, 60.0f })
        {
            float score = 0.0f;
            int strong = 0;

            for (int h = 1; h <= 6; ++h)
            {
                const float p = prominence (a, f0 * (float) h, 2.0f, 25.0f);
                score += juce::jmax (0.0f, p);
                strong += p > 6.0f ? 1 : 0;
            }

            if (strong >= 2 && score > bestScore)
            {
                bestScore = score;
                a.detectedHumHz = f0;
            }
        }
    }

    // 4. Persistent narrow tones (fan whine, buzz, whistle) in the steady floor.
    {
        const double binHz = a.sampleRate / a.displayFftSize;
        struct Peak { float hz, prom; };
        std::vector<Peak> peaks;
        const int lo = (int) (80.0 / binHz), hi = juce::jmin ((int) a.floorDb.size() - 3, (int) (16000.0 / binHz));

        for (int k = juce::jmax (3, lo); k < hi; ++k)
        {
            const float v = a.floorDb[(size_t) k];

            if (v <= a.floorDb[(size_t) (k - 1)] || v < a.floorDb[(size_t) (k + 1)]
                || v <= a.floorDb[(size_t) (k - 2)] || v < a.floorDb[(size_t) (k + 2)])
                continue;

            const float hz = (float) (k * binHz);
            const float p = prominence (a, hz, (float) binHz, juce::jmax (20.0f, hz * 0.06f));

            if (p < 12.0f || v < -100.0f)
                continue;

            if (a.detectedHumHz > 0.0f)
            {
                const float h = hz / a.detectedHumHz;
                if (std::abs (h - std::round (h)) * a.detectedHumHz < 3.0f && std::round (h) <= 12.0f)
                    continue;   // part of the hum, handled by the hum remover
            }

            peaks.push_back ({ hz, p });
        }

        std::sort (peaks.begin(), peaks.end(), [] (const Peak& x, const Peak& y) { return x.prom > y.prom; });

        for (const auto& p : peaks)
        {
            const bool duplicate = std::any_of (a.suggestedTonesHz.begin(), a.suggestedTonesHz.end(),
                                                [&p] (float t) { return std::abs (t - p.hz) < t * 0.03f; });
            if (! duplicate)
                a.suggestedTonesHz.push_back (p.hz);

            if (a.suggestedTonesHz.size() >= 6)
                break;
        }

        std::sort (a.suggestedTonesHz.begin(), a.suggestedTonesHz.end());
    }

    return a;
}

NoiseProfile learnProfile (const juce::AudioBuffer<float>& audio, double sampleRate, int startSample, int numSamples)
{
    NoiseProfile p;
    const int n = getProcessingFftSize (sampleRate);
    startSample = juce::jlimit (0, juce::jmax (0, audio.getNumSamples() - 1), startSample);
    numSamples = juce::jlimit (0, audio.getNumSamples() - startSample, numSamples);

    if (numSamples <= 0)
        return p;

    const auto fp = framePowers (audio, startSample, numSamples, n, sqrtHann (n), 100000, {});
    p.sampleRate = sampleRate;
    p.fftSize = n;
    p.power.assign ((size_t) fp.bins, 0.0f);
    p.learnedFromSelection = true;

    for (int f = 0; f < fp.numFrames(); ++f)
        for (int k = 0; k < fp.bins; ++k)
            p.power[(size_t) k] += fp.power[(size_t) f * (size_t) fp.bins + (size_t) k] / (float) fp.numFrames();

    return p;
}

float resolveHumHz (const CleanupSettings& s, const CleanupAnalysis& a)
{
    if (s.humHz == 50 || s.humHz == 60)
        return (float) s.humHz;

    return a.detectedHumHz > 0.0f ? a.detectedHumHz : 60.0f;
}

juce::AudioBuffer<float> process (const juce::AudioBuffer<float>& input, double sampleRate,
                                  const CleanupSettings& s, const NoiseProfile& profile,
                                  float humHz, ShouldCancel cancel, std::function<void (float)> progress)
{
    juce::AudioBuffer<float> out (input);

    if (! s.hasAnyEffect())
        return out;

    const int numCh = out.getNumChannels();
    const int length = out.getNumSamples();

    // 1. Hum, tone notches and rumble: zero-phase IIR filtering
    const auto filters = designFilters (s, humHz, sampleRate);

    if (! filters.empty())
    {
        const int pad = (int) juce::jmin ((double) length - 1, sampleRate);   // up to 1 s of reflected padding

        for (int ch = 0; ch < numCh; ++ch)
        {
            if (cancel && cancel())
                return {};

            filtfilt (filters, out.getWritePointer (ch), length, pad);
        }
    }

    if (progress)
        progress (0.15f);

    // 2. Steady noise: spectral gating against the noise profile (stereo-linked gains)
    if (! s.denoise || ! profile.isValid() || std::abs (profile.sampleRate - sampleRate) > 1.0 || length == 0)
    {
        if (progress)
            progress (1.0f);

        return out;
    }

    const int n = profile.fftSize;
    const int hop = n / 4;
    const int bins = n / 2 + 1;
    const auto window = sqrtHann (n);
    const int numFrames = (length + n - hop) / hop + 1;
    auto framePos = [n, hop] (int f) { return f * hop - (n - hop); };

    const float floorGain = juce::Decibels::decibelsToGain (-juce::jlimit (0.0f, 60.0f, s.reductionDb));
    const float alpha = 1.0f + 4.0f * juce::jlimit (0.0f, 1.0f, s.sensitivity);   // over-subtraction
    const float release = (float) std::exp (-(double) hop / (0.08 * sampleRate));

    juce::dsp::FFT fft (fftOrderFor (n));
    std::vector<std::vector<float>> spec ((size_t) numCh, std::vector<float> ((size_t) n * 2));
    std::vector<float> power ((size_t) bins), gain ((size_t) bins), smoothGain ((size_t) bins, 1.0f), raw ((size_t) bins);

    juce::AudioBuffer<float> result (numCh, length);
    result.clear();
    std::vector<float> norm ((size_t) length, 0.0f);

    for (int f = 0; f < numFrames; ++f)
    {
        if ((f & 127) == 0)
        {
            if (cancel && cancel())
                return {};

            if (progress)
                progress (0.15f + 0.85f * (float) f / (float) numFrames);
        }

        const int pos = framePos (f);
        std::fill (power.begin(), power.end(), 0.0f);

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& buf = spec[(size_t) ch];
            std::fill (buf.begin(), buf.end(), 0.0f);
            const float* src = out.getReadPointer (ch);

            for (int i = 0; i < n; ++i)
            {
                const int t = pos + i;
                if (t >= 0 && t < length)
                    buf[(size_t) i] = src[t] * window[(size_t) i];
            }

            fft.performRealOnlyForwardTransform (buf.data(), true);

            for (int k = 0; k < bins; ++k)
            {
                const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)];
                power[(size_t) k] += (re * re + im * im) / (float) numCh;
            }
        }

        // Spectral subtraction gain on frequency-smoothed power (less "musical noise")
        for (int k = 0; k < bins; ++k)
        {
            const float pl = power[(size_t) juce::jmax (0, k - 1)], pr = power[(size_t) juce::jmin (bins - 1, k + 1)];
            const float p = 0.25f * pl + 0.5f * power[(size_t) k] + 0.25f * pr;
            const float g2 = 1.0f - alpha * profile.power[(size_t) k] / juce::jmax (p, 1.0e-20f);
            raw[(size_t) k] = juce::jmax (floorGain, std::sqrt (juce::jmax (0.0f, g2)));
        }

        // Smooth across frequency, then release slowly over time (instant attack keeps transients)
        for (int k = 0; k < bins; ++k)
        {
            const float g = 0.25f * raw[(size_t) juce::jmax (0, k - 1)] + 0.5f * raw[(size_t) k]
                          + 0.25f * raw[(size_t) juce::jmin (bins - 1, k + 1)];
            float& sg = smoothGain[(size_t) k];
            sg = g >= sg ? g : release * sg + (1.0f - release) * g;
            gain[(size_t) k] = sg;
        }

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& buf = spec[(size_t) ch];

            for (int k = 0; k < bins; ++k)
            {
                buf[(size_t) (2 * k)] *= gain[(size_t) k];
                buf[(size_t) (2 * k + 1)] *= gain[(size_t) k];
            }

            for (int k = bins; k < n; ++k)   // Hermitian mirror for the inverse transform
            {
                buf[(size_t) (2 * k)] = buf[(size_t) (2 * (n - k))];
                buf[(size_t) (2 * k + 1)] = -buf[(size_t) (2 * (n - k) + 1)];
            }

            fft.performRealOnlyInverseTransform (buf.data());
            float* dst = result.getWritePointer (ch);

            for (int i = 0; i < n; ++i)
            {
                const int t = pos + i;
                if (t >= 0 && t < length)
                    dst[t] += buf[(size_t) i] * window[(size_t) i];
            }
        }

        for (int i = 0; i < n; ++i)
        {
            const int t = pos + i;
            if (t >= 0 && t < length)
                norm[(size_t) t] += window[(size_t) i] * window[(size_t) i];
        }
    }

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* d = result.getWritePointer (ch);
        for (int i = 0; i < length; ++i)
            d[i] = norm[(size_t) i] > 1.0e-6f ? d[i] / norm[(size_t) i] : 0.0f;
    }

    if (progress)
        progress (1.0f);

    return result;
}

float getFilterResponseDb (const CleanupSettings& s, float humHz, double sampleRate, float hz)
{
    double mag = 1.0;

    for (const auto& f : designFilters (s, humHz, sampleRate))
        mag *= f.magnitude (sampleRate, hz);

    return juce::Decibels::gainToDecibels ((float) (mag * mag), -120.0f);   // forward + backward
}

float snapToPeak (const CleanupAnalysis& a, float hz, float searchOctaves)
{
    if (! a.isValid())
        return hz;

    const double binHz = a.sampleRate / a.displayFftSize;
    const int lo = juce::jmax (1, (int) (hz * std::pow (2.0f, -searchOctaves) / binHz));
    const int hi = juce::jmin ((int) a.floorDb.size() - 2, (int) std::ceil (hz * std::pow (2.0f, searchOctaves) / binHz));

    int best = -1;
    float bestValue = -1000.0f;

    for (int k = lo; k <= hi; ++k)
    {
        const float v = juce::jmax (a.floorDb[(size_t) k], a.averageDb[(size_t) k] - 6.0f);
        if (v > bestValue && v >= a.floorDb[(size_t) (k - 1)] && v >= a.floorDb[(size_t) (k + 1)])
        {
            bestValue = v;
            best = k;
        }
    }

    if (best < 0)
        return hz;

    // Parabolic interpolation for sub-bin accuracy
    const float y0 = a.floorDb[(size_t) (best - 1)], y1 = a.floorDb[(size_t) best], y2 = a.floorDb[(size_t) (best + 1)];
    const float denom = y0 - 2.0f * y1 + y2;
    const float offset = std::abs (denom) > 1.0e-6f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (y0 - y2) / denom) : 0.0f;
    return (float) ((best + offset) * binHz);
}

} // namespace NoiseCleanup
} // namespace stemsplitter
