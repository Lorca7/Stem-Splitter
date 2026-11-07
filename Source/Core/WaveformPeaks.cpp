#include "WaveformPeaks.h"

namespace stemsplitter
{

WaveformPeaks::WaveformPeaks (const juce::AudioBuffer<float>& buffer, int samplesPerBin)
    : binSize (juce::jmax (1, samplesPerBin)), numSamples (buffer.getNumSamples())
{
    const int numBins = (numSamples + binSize - 1) / binSize;
    mins.assign ((size_t) numBins, 0.0f);
    maxs.assign ((size_t) numBins, 0.0f);

    for (int b = 0; b < numBins; ++b)
    {
        const int start = b * binSize;
        const int n = juce::jmin (binSize, numSamples - start);
        float lo = 0.0f, hi = 0.0f;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto r = juce::FloatVectorOperations::findMinAndMax (buffer.getReadPointer (ch, start), n);
            lo = juce::jmin (lo, r.getStart());
            hi = juce::jmax (hi, r.getEnd());
        }

        mins[(size_t) b] = lo;
        maxs[(size_t) b] = hi;
        overallPeak = juce::jmax (overallPeak, -lo, hi);
    }
}

juce::Range<float> WaveformPeaks::getRange (double startSample, double endSample) const noexcept
{
    if (mins.empty())
        return {};

    const int last = (int) mins.size() - 1;
    const int b0 = juce::jlimit (0, last, (int) (startSample / binSize));
    const int b1 = juce::jlimit (b0, last, (int) (endSample / binSize));

    float lo = 0.0f, hi = 0.0f;

    for (int b = b0; b <= b1; ++b)
    {
        lo = juce::jmin (lo, mins[(size_t) b]);
        hi = juce::jmax (hi, maxs[(size_t) b]);
    }

    return { lo, hi };
}

} // namespace stemsplitter
