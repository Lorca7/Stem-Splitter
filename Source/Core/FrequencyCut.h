#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>

namespace stemsplitter
{

struct FrequencyCutSettings
{
    enum class Mode : int { lowCut = 0, bandCut, highCut };

    bool enabled = false;
    Mode mode = Mode::bandCut;
    float lowHz = 250.0f;     ///< low cut: cut below this. band cut: lower edge.
    float highHz = 4000.0f;   ///< high cut: cut above this. band cut: upper edge.
    float amount = 1.0f;      ///< 0 = no change, 1 = fully removed

    /** Short human readable description, e.g. "Band cut 250 Hz–4 kHz". */
    juce::String describe() const;

    static juce::String formatHz (float hz);

    bool operator== (const FrequencyCutSettings& o) const noexcept
    {
        return enabled == o.enabled && mode == o.mode && juce::exactlyEqual (lowHz, o.lowHz)
            && juce::exactlyEqual (highHz, o.highHz) && juce::exactlyEqual (amount, o.amount);
    }

    bool operator!= (const FrequencyCutSettings& o) const noexcept { return ! (*this == o); }
};

/** Removes a frequency range by splitting the signal with Linkwitz-Riley crossovers and turning down
    the unwanted band. The bands sum back to a flat (all-pass) response, so the parts you keep are
    unaffected. Two stages are cascaded for 48 dB/oct slopes and a deep (> 50 dB) cut. Used for both real-time preview and offline export, so what you hear is
    exactly what you get. */
class FrequencyCutProcessor
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    /** Can be called before each block; cheap if nothing changed. */
    void setSettings (const FrequencyCutSettings& settings);

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    /** Magnitude response in dB of the given settings (for drawing). */
    static float getMagnitudeDb (const FrequencyCutSettings& settings, float frequencyHz);

    static constexpr float minFrequency = 20.0f;
    static constexpr float maxFrequency = 20000.0f;

private:
    using LR = juce::dsp::LinkwitzRileyFilter<float>;

    static float stageGain (float amount) noexcept;

    double sampleRate = 44100.0;
    int channels = 2;
    FrequencyCutSettings current;
    bool wasEnabled = false;

    static constexpr int numStages = 2;

    struct Stage
    {
        LR lowSplit, highSplit, highAllpass;
    };

    std::array<Stage, numStages> stages;
    juce::SmoothedValue<float> bandGain { 1.0f };  // per-stage gain of the band being removed
    juce::SmoothedValue<float> wet { 0.0f };       // cross-fades the effect in/out when toggled
};

} // namespace stemsplitter
