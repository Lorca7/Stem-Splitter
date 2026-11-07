#pragma once

#include <juce_core/juce_core.h>
#include <array>

namespace stemsplitter
{

/** The instruments the engine can isolate. The order is the display order in the UI. */
enum class StemType : int
{
    vocals = 0,
    drums,
    bass,
    guitar,
    piano,
    other,
    numTypes
};

constexpr int numStemTypes = static_cast<int> (StemType::numTypes);

inline constexpr int toIndex (StemType t) noexcept   { return static_cast<int> (t); }
inline constexpr StemType fromIndex (int i) noexcept { return static_cast<StemType> (i); }

inline juce::String getStemName (StemType t)
{
    switch (t)
    {
        case StemType::vocals:   return "Vocals";
        case StemType::drums:    return "Drums";
        case StemType::bass:     return "Bass";
        case StemType::guitar:   return "Guitar";
        case StemType::piano:    return "Piano";
        case StemType::other:    return "Other";
        case StemType::numTypes: break;
    }

    return {};
}

/** The separation engines on offer. */
enum class EngineType : int
{
    fourStems = 0,  ///< Demucs v4 hybrid transformer, 4 sources
    sixStems,       ///< Demucs v4 hybrid transformer, 6 sources (adds guitar + piano)
    fourStemsHQ,    ///< Demucs v4 fine-tuned bag of 4 specialist models (best quality, ~4x slower)
    lite,           ///< Lightweight spectral DSP separation (instant, lower quality, needs no model)
    numEngines
};

constexpr int numEngineTypes = static_cast<int> (EngineType::numEngines);

inline juce::String getEngineName (EngineType e)
{
    switch (e)
    {
        case EngineType::fourStems:   return "4 Stems";
        case EngineType::sixStems:    return "6 Stems";
        case EngineType::fourStemsHQ: return "4 Stems HQ";
        case EngineType::lite:        return "Lite";
        case EngineType::numEngines:  break;
    }

    return {};
}

inline juce::String getEngineDescription (EngineType e)
{
    switch (e)
    {
        case EngineType::fourStems:   return "Vocals, drums, bass and other. Neural network (Demucs v4).";
        case EngineType::sixStems:    return "Adds guitar and piano. Neural network (Demucs v4 6-source).";
        case EngineType::fourStemsHQ: return "Four specialist networks, one per stem. Best quality, about 4x slower.";
        case EngineType::lite:        return "Instant spectral separation. No download needed, lower quality.";
        case EngineType::numEngines:  break;
    }

    return {};
}

inline juce::Array<StemType> getStemsForEngine (EngineType e)
{
    if (e == EngineType::sixStems)
        return { StemType::vocals, StemType::drums, StemType::bass, StemType::guitar, StemType::piano, StemType::other };

    return { StemType::vocals, StemType::drums, StemType::bass, StemType::other };
}

/** The sample rate every separation engine runs at internally. */
constexpr double engineSampleRate = 44100.0;

} // namespace stemsplitter
