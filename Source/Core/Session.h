#pragma once

#include "AudioIO.h"
#include "FrequencyCut.h"
#include "Separator.h"
#include "WaveformPeaks.h"

#include <array>
#include <memory>

namespace stemsplitter
{

/** The separated stems of one source file, at the source's sample rate. Immutable once created. */
struct StemSet
{
    EngineType engine = EngineType::fourStems;
    std::vector<StemAudio> stems;
    std::vector<WaveformPeaks> peaks;   // parallel to `stems`
    double secondsTaken = 0.0;

    const StemAudio* find (StemType t) const noexcept
    {
        for (auto& s : stems)
            if (s.type == t)
                return &s;

        return nullptr;
    }
};

/** Mixer settings for one stem. */
struct StemMixState
{
    float gainDb = 0.0f;
    bool muted = false;
    bool solo = false;
};

/** Everything that determines what you hear and what a "mix" export contains. */
struct MixSettings
{
    std::array<StemMixState, numStemTypes> stems {};
    FrequencyCutSettings cut;

    bool anySolo() const noexcept
    {
        for (auto& s : stems)
            if (s.solo)
                return true;

        return false;
    }

    /** Linear gain a stem contributes with, after mute/solo. */
    float getEffectiveGain (StemType t) const noexcept
    {
        const auto& s = stems[(size_t) toIndex (t)];

        if (s.muted || (anySolo() && ! s.solo))
            return 0.0f;

        return juce::Decibels::decibelsToGain (s.gainDb, -100.0f);
    }
};

} // namespace stemsplitter
