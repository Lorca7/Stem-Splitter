#pragma once

#include "Session.h"

namespace stemsplitter::MixRenderer
{

/** Renders the stems according to the mixer settings (mute/solo/gain) at the source sample rate.
    Without stems, the original audio is used. Optionally applies the frequency cut. */
juce::AudioBuffer<float> renderMix (const SourceAudio& source, const StemSet* stems,
                                    const MixSettings& mix, bool applyFrequencyCut);

/** e.g. "No Vocals", "Vocals Only", "Drums + Bass", "No Vocals, Low Cut 120 Hz". */
juce::String describeMix (const StemSet* stems, const MixSettings& mix, bool includeFrequencyCut);

struct ExportOptions
{
    AudioIO::BitDepth bitDepth = AudioIO::BitDepth::int24;
    double sampleRate = 0.0;          ///< 0 = keep the source's sample rate
    bool keepMonoIfSourceIsMono = true;
};

using ProgressFn = std::function<bool (float)>;   ///< return false to cancel

/** Writes one WAV per stem into `folder` as "<song> - <Stem>.wav". */
juce::Result exportStems (const SourceAudio& source, const StemSet& stems, const juce::Array<StemType>& which,
                          const juce::File& folder, const ExportOptions& options,
                          juce::Array<juce::File>& written, ProgressFn progress = {});

/** Writes a single WAV of the mix as heard (stems removed/muted, frequency cut applied). */
juce::Result exportMix (const SourceAudio& source, const StemSet* stems, const MixSettings& mix,
                        const juce::File& file, const ExportOptions& options, ProgressFn progress = {});

/** Converts a buffer at the source rate to what will be written (rate + channel count). */
juce::AudioBuffer<float> prepareForExport (const juce::AudioBuffer<float>& audio, const SourceAudio& source,
                                           const ExportOptions& options, double& outRate);

} // namespace stemsplitter::MixRenderer
