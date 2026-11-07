#pragma once

#include "StemTypes.h"
#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <vector>

namespace stemsplitter
{

struct StemAudio
{
    StemType type = StemType::other;
    juce::AudioBuffer<float> buffer;
};

/** Hooks a separator uses to report progress and check for cancellation (called from worker threads). */
struct SeparationContext
{
    std::function<bool()> shouldCancel = [] { return false; };
    std::function<void (float progress, const juce::String& stage)> reportProgress = [] (float, const juce::String&) {};
    int numThreads = 1;
};

/** Thrown from inside an engine to abort quickly when the user cancels. */
struct SeparationCancelled {};

/** A source-separation engine. Input is stereo at engineSampleRate; output stems have the same
    length and channel count as the input. */
class Separator
{
public:
    virtual ~Separator() = default;

    virtual juce::Result separate (const juce::AudioBuffer<float>& stereoInput,
                                   std::vector<StemAudio>& stems,
                                   SeparationContext& context) = 0;
};

std::unique_ptr<Separator> createSeparator (EngineType engine);

} // namespace stemsplitter
