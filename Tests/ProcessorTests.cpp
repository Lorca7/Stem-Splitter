// Headless tests for the plug-in processor: real-time playback, resampling, looping, stem muting,
// state save/restore and an editor smoke test.

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <iostream>

using namespace stemsplitter;

namespace
{
    int failures = 0;

    void check (bool condition, const juce::String& what)
    {
        std::cout << (condition ? "  ok    " : "  FAIL  ") << what << std::endl;
        if (! condition)
            ++failures;
    }

    std::shared_ptr<SourceAudio> makeSource (double sampleRate, double seconds)
    {
        auto src = std::make_shared<SourceAudio>();
        src->file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("test song.wav");
        src->sampleRate = sampleRate;
        src->originalNumChannels = 2;
        src->bitsPerSample = 24;
        src->buffer.setSize (2, (int) (sampleRate * seconds));
        juce::Random rng (7);

        for (int i = 0; i < src->buffer.getNumSamples(); ++i)
        {
            const double t = i / sampleRate;
            const float drum = (i % (int) (sampleRate / 2)) < 300 ? 0.4f * (rng.nextFloat() * 2.0f - 1.0f) : 0.0f;
            src->buffer.setSample (0, i, 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t) + drum);
            src->buffer.setSample (1, i, 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * t) + drum);
        }

        return src;
    }

    /** Renders `seconds` of output from the processor in device-sized blocks. */
    juce::AudioBuffer<float> render (StemSplitterProcessor& p, double deviceRate, double seconds, int blockSize = 512)
    {
        const int total = (int) (deviceRate * seconds);
        juce::AudioBuffer<float> out (2, total);
        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;

        for (int pos = 0; pos < total; pos += blockSize)
        {
            const int n = juce::jmin (blockSize, total - pos);
            juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, n);
            p.processBlock (view, midi);

            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, view, ch, 0, n);
        }

        return out;
    }

    float maxDifference (const juce::AudioBuffer<float>& a, int aStart, const juce::AudioBuffer<float>& b, int bStart, int n)
    {
        float worst = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                worst = juce::jmax (worst, std::abs (a.getSample (ch, aStart + i) - b.getSample (ch, bStart + i)));

        return worst;
    }

    float rms (const juce::AudioBuffer<float>& b, int start, int n)
    {
        double sum = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = start; i < start + n; ++i)
                sum += (double) b.getSample (ch, i) * b.getSample (ch, i);

        return (float) std::sqrt (sum / (n * b.getNumChannels()));
    }

    bool pumpUntil (std::function<bool()> condition, int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        while (juce::Time::getMillisecondCounter() < end)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

            if (condition())
                return true;
        }

        return condition();
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "Processor tests" << std::endl;

    auto source = makeSource (44100.0, 2.0);

    {
        StemSplitterProcessor p (true);
        p.adoptSource (source, WaveformPeaks (source->buffer));

        // 1. Same-rate playback is sample-exact (after the short de-click fade-in)
        p.prepareToPlay (44100.0, 512);
        p.setPlaying (true);
        auto out = render (p, 44100.0, 1.0);
        check (maxDifference (out, 512, source->buffer, 512, 44100 - 512) < 1.0e-6f, "same-rate playback reproduces the source exactly");
        check (p.getPlayPosition() == 44100, "play position advances by one second");

        // 2. Seeking
        p.seekToSample (22050);
        out = render (p, 44100.0, 0.25);
        check (maxDifference (out, 0, source->buffer, 22050, 4096) < 1.0e-6f, "seek jumps to the requested sample");

        // 3. Stop at the end and rewind
        p.seekToSample (source->getNumSamples() - 1000);
        render (p, 44100.0, 0.1);
        check (! p.isPlaying() && p.getPlayPosition() == 0, "playback stops at the end and rewinds");

        // 4. Looping keeps going
        p.setLooping (true);
        p.seekToSample (source->getNumSamples() - 1000);
        p.setPlaying (true);
        out = render (p, 44100.0, 0.1);
        check (p.isPlaying() && p.getPlayPosition() > 0 && p.getPlayPosition() < 10000, "looping wraps around to the start");
        check (maxDifference (out, 1000, source->buffer, 0, 2000) < 1.0e-6f, "loop wrap-around is seamless");
        p.setLooping (false);

        // 5. Different device rate: real-time resampling keeps pitch and timing
        p.prepareToPlay (48000.0, 480);
        p.seekToSample (0);
        p.setPlaying (true);
        out = render (p, 48000.0, 1.0, 480);
        const auto reference = AudioIO::resample (source->buffer, 44100.0, 48000.0);
        const float err = rms (out, 4800, 4800) > 0 ? maxDifference (out, 4800, reference, 4800, 4800) : 1.0f;
        check (std::abs ((int) p.getPlayPosition() - 44100) < 8, "48 kHz device consumes 44.1 kHz source at the right speed");
        check (err < 0.02f, "resampled playback matches the offline resampler (max diff " + juce::String (err, 5) + ")");

        // 6. Separate with the Lite engine, then check mute/solo in playback
        p.setPlaying (false);
        p.prepareToPlay (44100.0, 512);
        p.setEngine (EngineType::lite);
        p.startSeparation();
        const bool separated = pumpUntil ([&p] { return ! p.isSeparating() && p.getStems() != nullptr; }, 60000);
        check (separated, "Lite separation completes through the processor");

        if (separated)
        {
            const auto stems = p.getStems();
            check (stems->stems.size() == 4, "four stems produced");

            p.seekToSample (0);
            p.setPlaying (true);
            out = render (p, 44100.0, 1.0);
            check (maxDifference (out, 512, source->buffer, 512, 40000) < 1.0e-3f, "all stems together play back the original mix");

            for (auto type : { StemType::vocals, StemType::drums, StemType::bass, StemType::other })
                p.setStemMuted (type, true);

            render (p, 44100.0, 0.05);   // let the gain ramps settle
            out = render (p, 44100.0, 0.25);
            check (rms (out, 0, out.getNumSamples()) < 1.0e-6f, "muting every stem gives silence");

            auto mix = p.getMix();
            for (auto& s : mix.stems) s.muted = false;
            mix.stems[(size_t) toIndex (StemType::bass)].solo = true;
            p.setMix (mix);
            render (p, 44100.0, 0.05);
            const auto pos = (int) p.getPlayPosition();
            out = render (p, 44100.0, 0.25);
            check (maxDifference (out, 0, stems->find (StemType::bass)->buffer, pos, 8000) < 1.0e-5f, "solo plays only the soloed stem");

            // Offline mix render matches what the processor plays
            const auto offline = MixRenderer::renderMix (*source, stems.get(), p.getMix(), true);
            check (maxDifference (out, 0, offline, pos, 8000) < 1.0e-5f, "preview and mix export render identically");
        }

        // 7. State round trip
        auto mix = p.getMix();
        mix.stems[(size_t) toIndex (StemType::vocals)].gainDb = -4.5f;
        mix.cut.enabled = true;
        mix.cut.mode = FrequencyCutSettings::Mode::highCut;
        mix.cut.highHz = 9000.0f;
        p.setMix (mix);
        auto prefs = p.getExportPreferences();
        prefs.mode = ExportMode::mix;
        prefs.bitDepth = AudioIO::BitDepth::int16;
        p.setExportPreferences (prefs);

        juce::MemoryBlock state;
        p.getStateInformation (state);

        StemSplitterProcessor restored (false);
        restored.setStateInformation (state.getData(), (int) state.getSize());
        check (juce::approximatelyEqual (restored.getMix().stems[(size_t) toIndex (StemType::vocals)].gainDb, -4.5f)
                   && restored.getMix().cut == mix.cut
                   && restored.getExportPreferences().mode == ExportMode::mix
                   && restored.getExportPreferences().bitDepth == AudioIO::BitDepth::int16
                   && restored.getEngine() == EngineType::lite,
               "plug-in state saves and restores");

        // 8. Editor smoke test: build, lay out, refresh and destroy the full UI
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            editor->setSize (1180, 760);
            editor->setSize (980, 640);
            p.sendSynchronousChangeMessage();
            juce::Image img (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
            juce::Graphics g (img);
            editor->paintEntireComponent (g, false);
            check (true, "editor builds, lays out and paints");
        }
    }

    std::cout << (failures == 0 ? "All processor tests passed." : juce::String (failures) + " test(s) failed.") << std::endl;
    return failures == 0 ? 0 : 1;
}
