#include "MixRenderer.h"

namespace stemsplitter::MixRenderer
{

juce::AudioBuffer<float> renderMix (const SourceAudio& source, const StemSet* stems,
                                    const MixSettings& mix, bool applyFrequencyCut)
{
    const int length = source.getNumSamples();
    juce::AudioBuffer<float> out (2, length);
    out.clear();

    if (stems != nullptr && ! stems->stems.empty())
    {
        for (const auto& stem : stems->stems)
        {
            const float g = mix.getEffectiveGain (stem.type);

            if (g <= 0.0f)
                continue;

            const int n = juce::jmin (length, stem.buffer.getNumSamples());

            for (int ch = 0; ch < 2; ++ch)
                out.addFrom (ch, 0, stem.buffer, juce::jmin (ch, stem.buffer.getNumChannels() - 1), 0, n, g);
        }
    }
    else
    {
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, 0, source.buffer, juce::jmin (ch, source.buffer.getNumChannels() - 1), 0, length);
    }

    if (applyFrequencyCut && mix.cut.enabled && mix.cut.amount > 0.0f)
    {
        FrequencyCutProcessor cut;
        cut.setSettings (mix.cut);
        cut.prepare (source.sampleRate, 2);
        cut.process (out, 0, length);
    }

    return out;
}

juce::String describeMix (const StemSet* stems, const MixSettings& mix, bool includeFrequencyCut)
{
    juce::StringArray parts;

    if (stems != nullptr && ! stems->stems.empty())
    {
        juce::StringArray kept, removed;

        for (const auto& s : stems->stems)
            (mix.getEffectiveGain (s.type) > 0.0f ? kept : removed).add (getStemName (s.type));

        if (kept.isEmpty())
            parts.add ("Silence");
        else if (removed.isEmpty())
            parts.add ("Mix");
        else if (kept.size() == 1)
            parts.add (kept[0] + " Only");
        else if (removed.size() <= kept.size())
            parts.add ("No " + removed.joinIntoString (" & "));
        else
            parts.add (kept.joinIntoString (" + "));
    }

    if (includeFrequencyCut && mix.cut.enabled && mix.cut.amount > 0.0f)
        parts.add (mix.cut.describe());

    if (parts.isEmpty() || (parts.size() == 1 && parts[0] == "Mix"))
        return "Mix";

    parts.removeString ("Mix");
    return parts.joinIntoString (" + ");
}

juce::AudioBuffer<float> prepareForExport (const juce::AudioBuffer<float>& audio, const SourceAudio& source,
                                           const ExportOptions& options, double& outRate)
{
    outRate = options.sampleRate > 0.0 ? options.sampleRate : source.sampleRate;

    const int channels = (options.keepMonoIfSourceIsMono && source.originalNumChannels == 1) ? 1 : 2;
    auto result = AudioIO::withChannelCount (audio, channels);

    if (std::abs (outRate - source.sampleRate) > 0.5)
        result = AudioIO::resample (result, source.sampleRate, outRate);

    return result;
}

juce::Result exportStems (const SourceAudio& source, const StemSet& stems, const juce::Array<StemType>& which,
                          const juce::File& folder, const ExportOptions& options,
                          juce::Array<juce::File>& written, ProgressFn progress)
{
    if (which.isEmpty())
        return juce::Result::fail ("Select at least one stem to export.");

    const auto songName = AudioIO::makeSafeFileName (source.file.getFileNameWithoutExtension());
    int index = 0;

    for (auto type : which)
    {
        const auto* stem = stems.find (type);

        if (stem == nullptr)
            continue;

        if (progress && ! progress ((float) index / (float) which.size()))
            return juce::Result::fail ("Cancelled");

        double rate = 0.0;
        const auto audio = prepareForExport (stem->buffer, source, options, rate);
        const auto file = AudioIO::getNonexistentSibling (folder.getChildFile (songName + " - " + getStemName (type) + ".wav"));

        auto result = AudioIO::writeWav (file, audio, { options.bitDepth, rate });

        if (result.failed())
            return result;

        written.add (file);
        ++index;
    }

    if (progress)
        progress (1.0f);

    return juce::Result::ok();
}

juce::Result exportMix (const SourceAudio& source, const StemSet* stems, const MixSettings& mix,
                        const juce::File& file, const ExportOptions& options, ProgressFn progress)
{
    if (progress && ! progress (0.1f))
        return juce::Result::fail ("Cancelled");

    const auto rendered = renderMix (source, stems, mix, true);

    if (progress && ! progress (0.5f))
        return juce::Result::fail ("Cancelled");

    double rate = 0.0;
    const auto audio = prepareForExport (rendered, source, options, rate);
    auto result = AudioIO::writeWav (file, audio, { options.bitDepth, rate });

    if (progress)
        progress (1.0f);

    return result;
}

} // namespace stemsplitter::MixRenderer
