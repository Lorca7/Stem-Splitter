#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace stemsplitter
{

/** A compact min/max overview of a buffer for drawing waveforms quickly at any zoom. */
class WaveformPeaks
{
public:
    WaveformPeaks() = default;
    explicit WaveformPeaks (const juce::AudioBuffer<float>& buffer, int samplesPerBin = 256);

    bool isEmpty() const noexcept               { return mins.empty(); }
    int getNumSourceSamples() const noexcept    { return numSamples; }

    /** Min/max over the sample range [start, end). */
    juce::Range<float> getRange (double startSample, double endSample) const noexcept;

    /** Peak absolute level over the whole buffer. */
    float getOverallPeak() const noexcept       { return overallPeak; }

private:
    int binSize = 256;
    int numSamples = 0;
    float overallPeak = 0.0f;
    std::vector<float> mins, maxs;
};

} // namespace stemsplitter
