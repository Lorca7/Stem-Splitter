#include "FrequencyCut.h"

#include <complex>

namespace stemsplitter
{

juce::String FrequencyCutSettings::formatHz (float hz)
{
    if (hz >= 1000.0f)
    {
        const auto khz = hz / 1000.0f;
        return juce::String (khz, khz >= 10.0f ? 1 : 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " kHz";
    }

    return juce::String (juce::roundToInt (hz)) + " Hz";
}

juce::String FrequencyCutSettings::describe() const
{
    juce::String s;

    switch (mode)
    {
        case Mode::lowCut:  s = "Low Cut " + formatHz (lowHz); break;
        case Mode::highCut: s = "High Cut " + formatHz (highHz); break;
        case Mode::bandCut: s = "Band Cut " + formatHz (lowHz) + " to " + formatHz (highHz); break;
    }

    if (amount < 0.999f)
        s << " " << juce::roundToInt (amount * 100.0f) << "%";

    return s;
}

//==============================================================================
void FrequencyCutProcessor::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate;
    channels = juce::jmax (1, numChannels);

    const juce::dsp::ProcessSpec spec { sampleRate, 4096, (juce::uint32) channels };

    for (auto& st : stages)
    {
        for (auto* f : { &st.lowSplit, &st.highSplit, &st.highAllpass })
            f->prepare (spec);

        st.lowSplit.setType (LR::Type::lowpass);
        st.highSplit.setType (LR::Type::lowpass);
        st.highAllpass.setType (LR::Type::allpass);
    }

    bandGain.reset (sampleRate, 0.03);
    wet.reset (sampleRate, 0.03);

    auto s = current;
    current = {};
    current.enabled = ! s.enabled;   // force an update
    setSettings (s);
    bandGain.setCurrentAndTargetValue (stageGain (s.amount));
    wet.setCurrentAndTargetValue (s.enabled ? 1.0f : 0.0f);
    reset();
}

void FrequencyCutProcessor::reset()
{
    for (auto& st : stages)
        for (auto* f : { &st.lowSplit, &st.highSplit, &st.highAllpass })
            f->reset();
}

void FrequencyCutProcessor::setSettings (const FrequencyCutSettings& s)
{
    if (s == current)
        return;

    const auto nyquistLimit = (float) juce::jmin ((double) maxFrequency, sampleRate * 0.45);
    auto lo = juce::jlimit (minFrequency, nyquistLimit, s.lowHz);
    auto hi = juce::jlimit (minFrequency, nyquistLimit, s.highHz);

    if (s.mode == FrequencyCutSettings::Mode::bandCut)
    {
        // keep the band edges at least a third of an octave apart
        hi = juce::jmax (hi, lo * 1.26f);
        if (hi > nyquistLimit) { hi = nyquistLimit; lo = juce::jmin (lo, hi / 1.26f); }
    }

    if (s.mode != current.mode)
        reset();

    for (auto& st : stages)
    {
        st.lowSplit.setCutoffFrequency (lo);
        st.highSplit.setCutoffFrequency (hi);
        st.highAllpass.setCutoffFrequency (hi);
    }

    bandGain.setTargetValue (stageGain (s.amount));
    wet.setTargetValue (s.enabled ? 1.0f : 0.0f);
    current = s;
}

void FrequencyCutProcessor::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (! current.enabled && ! wet.isSmoothing())
    {
        if (wasEnabled)
        {
            reset();
            wasEnabled = false;
        }

        return;
    }

    wasEnabled = true;
    const int numCh = juce::jmin (channels, buffer.getNumChannels());
    const auto mode = current.mode;

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = bandGain.getNextValue();
        const float w = wet.getNextValue();

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* data = buffer.getWritePointer (ch, startSample);
            const float x = data[i];
            float y = x;

            for (auto& st : stages)
            {
                switch (mode)
                {
                    case FrequencyCutSettings::Mode::lowCut:
                    {
                        float low = 0.0f, high = 0.0f;
                        st.lowSplit.processSample (ch, y, low, high);
                        y = high + g * low;
                        break;
                    }

                    case FrequencyCutSettings::Mode::highCut:
                    {
                        float low = 0.0f, high = 0.0f;
                        st.highSplit.processSample (ch, y, low, high);
                        y = low + g * high;
                        break;
                    }

                    case FrequencyCutSettings::Mode::bandCut:
                    {
                        float low = 0.0f, rest = 0.0f, mid = 0.0f, high = 0.0f;
                        st.lowSplit.processSample (ch, y, low, rest);
                        st.highSplit.processSample (ch, rest, mid, high);
                        low = st.highAllpass.processSample (ch, low);   // phase-align the low band
                        y = low + high + g * mid;
                        break;
                    }
                }
            }

            // When toggling, cross-fade between the dry and the processed signal.
            data[i] = w >= 1.0f ? y : x + w * (y - x);
        }
    }

    for (auto& st : stages)
        for (auto* f : { &st.lowSplit, &st.highSplit, &st.highAllpass })
            f->snapToZero();
}

float FrequencyCutProcessor::stageGain (float amount) noexcept
{
    // Cascaded stages multiply, so each one gets the n-th root of the overall band gain.
    return std::pow (1.0f - juce::jlimit (0.0f, 1.0f, amount), 1.0f / (float) numStages);
}

float FrequencyCutProcessor::getMagnitudeDb (const FrequencyCutSettings& s, float f)
{
    using C = std::complex<double>;
    const double g = stageGain (s.amount);

    // Analogue Linkwitz-Riley 4th-order prototypes: LP = B², HP = (s²·B)², with B = 1/(s² + √2 s + 1)
    auto lp = [] (double freq, double fc) { const C sj (0.0, freq / fc); const C b = 1.0 / (sj * sj + std::sqrt (2.0) * sj + 1.0); return b * b; };
    auto hp = [] (double freq, double fc) { const C sj (0.0, freq / fc); const C b = sj * sj / (sj * sj + std::sqrt (2.0) * sj + 1.0); return b * b; };

    C h (1.0, 0.0);

    if (s.enabled)
    {
        switch (s.mode)
        {
            case FrequencyCutSettings::Mode::lowCut:
                h = hp (f, s.lowHz) + g * lp (f, s.lowHz);
                break;

            case FrequencyCutSettings::Mode::highCut:
                h = lp (f, s.highHz) + g * hp (f, s.highHz);
                break;

            case FrequencyCutSettings::Mode::bandCut:
            {
                const double hi = juce::jmax (s.highHz, s.lowHz * 1.26f);
                const C lowBand = lp (f, s.lowHz) * (lp (f, hi) + hp (f, hi));
                const C rest = hp (f, s.lowHz);
                h = lowBand + rest * hp (f, hi) + g * rest * lp (f, hi);
                break;
            }
        }
    }

    return juce::Decibels::gainToDecibels ((float) std::pow (std::abs (h), (double) numStages), -120.0f);
}

} // namespace stemsplitter
