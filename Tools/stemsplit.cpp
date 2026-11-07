// stemsplit: command-line front end for the Stem Splitter engine.
//
//   stemsplit song.wav                          → song - Vocals.wav, song - Drums.wav, ...
//   stemsplit --engine 6 song.wav               → six stems (adds guitar + piano)
//   stemsplit --remove vocals song.wav          → song (No Vocals).wav
//   stemsplit --cut band:250-4000 song.wav      → song (Band Cut 250 Hz-4 kHz).wav (no separation)
//   stemsplit --selftest                        → runs the built-in DSP checks

#include "Core/MixRenderer.h"
#include "Core/ModelManager.h"
#include "Core/SeparationJob.h"

#include <iostream>

using namespace stemsplitter;

namespace
{
    /** Value of "--name value" or "--name=value". */
    juce::String optionValue (const juce::ArgumentList& args, const juce::String& name)
    {
        for (int i = 0; i < args.size(); ++i)
        {
            const auto& text = args[i].text;

            if (text.startsWith (name + "="))
                return text.fromFirstOccurrenceOf ("=", false, false).unquoted();

            if (text == name && i + 1 < args.size())
                return args[i + 1].text.unquoted();
        }

        return {};
    }

    bool isOptionValue (const juce::ArgumentList& args, int index)
    {
        static const juce::StringArray valued { "--engine", "--out", "--stems", "--remove", "--cut", "--bits", "--rate", "--threads" };
        return index > 0 && valued.contains (args[index - 1].text);
    }

    void printUsage()
    {
        std::cout <<
R"(Usage: stemsplit [options] <audio file>

Options:
  --engine <4|6|hq|lite>   Separation engine (default 4)
  --out <folder>           Output folder (default: next to the input)
  --stems <list>           Only export these stems, e.g. vocals,drums
  --remove <list>          Export ONE file with these stems removed, e.g. vocals
  --cut <spec>             Frequency cut for the mix: low:<hz> | high:<hz> | band:<lo>-<hi>
  --bits <16|24|32>        Output bit depth (default 24; 32 = float)
  --rate <hz>              Output sample rate (default: same as input)
  --threads <n>            Worker threads (default: automatic)
  --list-models            Show where models are looked for and which are installed
  --selftest               Run built-in DSP self tests
)";
    }

    std::optional<StemType> parseStem (juce::String s)
    {
        s = s.trim().toLowerCase();

        for (int i = 0; i < numStemTypes; ++i)
            if (getStemName (fromIndex (i)).toLowerCase() == s)
                return fromIndex (i);

        return std::nullopt;
    }

    //==========================================================================
    int fail (const juce::String& message)
    {
        std::cerr << "FAIL: " << message << std::endl;
        return 1;
    }

    double rmsDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int skip = 0)
    {
        double sum = 0.0;
        int count = 0;
        const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());

        for (int ch = 0; ch < juce::jmin (a.getNumChannels(), b.getNumChannels()); ++ch)
            for (int i = skip; i < n - skip; ++i, ++count)
            {
                const double d = (double) a.getSample (ch, i) - (double) b.getSample (ch, i);
                sum += d * d;
            }

        return count > 0 ? std::sqrt (sum / count) : 0.0;
    }

    double rms (const juce::AudioBuffer<float>& a, int skip = 0)
    {
        juce::AudioBuffer<float> zero (a.getNumChannels(), a.getNumSamples());
        zero.clear();
        return rmsDifference (a, zero, skip);
    }

    juce::AudioBuffer<float> makeTestSignal (double sr, double seconds)
    {
        const int n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (2, n);
        juce::Random rng (42);

        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const float bass = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * t);
            const float voice = 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t
                                                          + 2.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t));
            const bool hit = (i % (int) (sr / 2)) < 400;
            const float drum = hit ? 0.5f * (rng.nextFloat() * 2.0f - 1.0f) : 0.0f;
            const float pad = 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1200.0 * t);

            b.setSample (0, i, 0.8f * (bass + voice + drum + pad));
            b.setSample (1, i, 0.8f * (bass + voice + drum - pad));
        }

        return b;
    }

    int runSelfTest()
    {
        std::cout << "Self test" << std::endl;

        // 1. Resampler: 48k → 44.1k → 48k round trip of a band-limited signal
        {
            juce::AudioBuffer<float> sig (1, 48000);
            for (int i = 0; i < sig.getNumSamples(); ++i)
                sig.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 48000.0)
                                   + 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 15000.0 * i / 48000.0));

            auto down = AudioIO::resample (sig, 48000.0, 44100.0);
            auto up = AudioIO::resample (down, 44100.0, 48000.0);

            if (down.getNumSamples() != 44100 || up.getNumSamples() != 48000)
                return fail ("resampler output length");

            const double err = rmsDifference (sig, up, 512);
            std::cout << "  resampler round-trip error: " << juce::Decibels::gainToDecibels (err) << " dB" << std::endl;

            if (err > 1.0e-3)
                return fail ("resampler round trip is not transparent");
        }

        // 2. Lite engine: stems must add back up to the input
        {
            const auto input = makeTestSignal (44100.0, 6.0);
            std::vector<StemAudio> stems;
            SeparationContext ctx;
            ctx.numThreads = 3;

            auto r = createSeparator (EngineType::lite)->separate (input, stems, ctx);

            if (r.failed() || stems.size() != 4)
                return fail ("lite separation: " + r.getErrorMessage());

            juce::AudioBuffer<float> sum (2, input.getNumSamples());
            sum.clear();

            for (auto& s : stems)
                for (int ch = 0; ch < 2; ++ch)
                    sum.addFrom (ch, 0, s.buffer, ch, 0, input.getNumSamples());

            const double err = rmsDifference (input, sum) / rms (input);
            std::cout << "  lite reconstruction error: " << juce::Decibels::gainToDecibels (err) << " dB" << std::endl;

            for (auto& s : stems)
                std::cout << "    " << getStemName (s.type) << " level " << juce::Decibels::gainToDecibels (rms (s.buffer)) << " dB" << std::endl;

            if (err > 1.0e-3)
                return fail ("lite stems don't sum to the input");

            const auto* bass = &stems[2];
            if (rms (bass->buffer) < 0.1)
                return fail ("lite bass stem is missing the 55 Hz tone");
        }

        // 3. Frequency cut: band cut removes 1 kHz, keeps 100 Hz and 10 kHz; disabled = bit transparent
        {
            const double sr = 48000.0;
            auto tone = [sr] (double hz)
            {
                juce::AudioBuffer<float> b (2, (int) sr * 2);
                for (int i = 0; i < b.getNumSamples(); ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        b.setSample (ch, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr));
                return b;
            };

            FrequencyCutSettings cut;
            cut.enabled = true;
            cut.mode = FrequencyCutSettings::Mode::bandCut;
            cut.lowHz = 400.0f;
            cut.highHz = 2500.0f;

            for (double hz : { 100.0, 1000.0, 10000.0 })
            {
                auto b = tone (hz);
                const double before = rms (b, 24000);
                FrequencyCutProcessor p;
                p.setSettings (cut);
                p.prepare (sr, 2);
                p.process (b, 0, b.getNumSamples());
                const double gainDb = juce::Decibels::gainToDecibels (rms (b, 24000) / before);
                const double predicted = FrequencyCutProcessor::getMagnitudeDb (cut, (float) hz);
                std::cout << "  band cut @ " << hz << " Hz: " << gainDb << " dB (predicted " << predicted << " dB)" << std::endl;

                if (std::abs (gainDb - predicted) > 1.0)
                    return fail ("frequency cut response doesn't match its display curve");

                if (juce::approximatelyEqual (hz, 1000.0) ? gainDb > -40.0 : gainDb < -1.0)
                    return fail ("frequency cut response");
            }

            auto b = tone (1000.0);
            const auto original = b;
            FrequencyCutProcessor p;
            p.prepare (sr, 2);
            p.process (b, 0, b.getNumSamples());

            if (rmsDifference (b, original) > 0.0)
                return fail ("disabled frequency cut must not alter the audio");
        }

        // 4. WAV round trip at each bit depth
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("stemsplit-selftest");
            dir.createDirectory();
            const auto sig = makeTestSignal (44100.0, 1.0);

            for (auto depth : { AudioIO::BitDepth::int16, AudioIO::BitDepth::int24, AudioIO::BitDepth::float32 })
            {
                const auto f = dir.getChildFile ("t" + juce::String ((int) depth) + ".wav");
                f.deleteFile();
                auto r = AudioIO::writeWav (f, sig, { depth, 44100.0 });

                SourceAudio back;
                if (r.wasOk())
                    r = AudioIO::loadFile (f, back);

                if (r.failed())
                    return fail ("wav " + juce::String ((int) depth) + ": " + r.getErrorMessage());

                const double err = rmsDifference (sig, back.buffer);
                std::cout << "  wav " << (int) depth << "-bit round trip error: " << juce::Decibels::gainToDecibels (err) << " dB" << std::endl;

                if (back.bitsPerSample != (int) depth || err > (depth == AudioIO::BitDepth::int16 ? 1.0e-4 : 1.0e-6))
                    return fail ("wav round trip");
            }

            dir.deleteRecursively();
        }

        std::cout << "All self tests passed." << std::endl;
        return 0;
    }

    void listModels()
    {
        std::cout << "Model search folders:" << std::endl;

        for (auto& d : ModelManager::getSearchDirectories())
            std::cout << "  " << d.getFullPathName() << (d.isDirectory() ? "" : "  (missing)") << std::endl;

        std::cout << "Engines:" << std::endl;

        for (int i = 0; i < numEngineTypes; ++i)
        {
            const auto e = (EngineType) i;
            std::cout << "  " << getEngineName (e).paddedRight (' ', 12) << (ModelManager::isInstalled (e) ? "installed" : "not installed");

            for (auto& f : ModelManager::getRequiredFiles (e))
                std::cout << "\n      " << f << "  " << ModelManager::getDownloadUrl (f).toString (false);

            std::cout << std::endl;
        }
    }
}

int main (int argc, char* argv[])
{
    juce::ArgumentList args (argc, argv);

    if (args.containsOption ("--help|-h") || args.size() == 0)
    {
        printUsage();
        return args.size() == 0 ? 1 : 0;
    }

    if (args.containsOption ("--selftest"))
        return runSelfTest();

    if (args.containsOption ("--list-models"))
    {
        listModels();
        return 0;
    }

    // Engine
    auto engine = EngineType::fourStems;
    const auto engineArg = optionValue (args, "--engine").toLowerCase();

    if (engineArg == "6") engine = EngineType::sixStems;
    else if (engineArg == "hq") engine = EngineType::fourStemsHQ;
    else if (engineArg == "lite") engine = EngineType::lite;
    else if (engineArg.isNotEmpty() && engineArg != "4") { printUsage(); return 1; }

    // Input
    juce::File input;

    for (int i = 0; i < args.size(); ++i)
    {
        const auto text = args[i].text.unquoted();

        if (! args[i].isOption() && ! isOptionValue (args, i) && text.isNotEmpty()
            && juce::File::getCurrentWorkingDirectory().getChildFile (text).existsAsFile())
            input = juce::File::getCurrentWorkingDirectory().getChildFile (text);
    }

    if (input == juce::File())
        return fail ("No input file given (or it doesn't exist).");

    const auto outArg = optionValue (args, "--out");
    const auto outDir = outArg.isNotEmpty() ? juce::File::getCurrentWorkingDirectory().getChildFile (outArg)
                                            : input.getParentDirectory();

    MixRenderer::ExportOptions exportOptions;
    const auto bits = optionValue (args, "--bits").getIntValue();
    exportOptions.bitDepth = bits == 16 ? AudioIO::BitDepth::int16 : bits == 32 ? AudioIO::BitDepth::float32 : AudioIO::BitDepth::int24;
    exportOptions.sampleRate = optionValue (args, "--rate").getDoubleValue();

    MixSettings mix;
    juce::Array<StemType> removed, selected;

    for (auto& s : juce::StringArray::fromTokens (optionValue (args, "--remove"), ",", ""))
        if (auto t = parseStem (s)) removed.add (*t); else return fail ("Unknown stem: " + s);

    for (auto& s : juce::StringArray::fromTokens (optionValue (args, "--stems"), ",", ""))
        if (auto t = parseStem (s)) selected.add (*t); else return fail ("Unknown stem: " + s);

    const auto cutArg = optionValue (args, "--cut").toLowerCase();

    if (cutArg.isNotEmpty())
    {
        mix.cut.enabled = true;
        const auto value = cutArg.fromFirstOccurrenceOf (":", false, false);

        if (cutArg.startsWith ("low:"))       { mix.cut.mode = FrequencyCutSettings::Mode::lowCut;  mix.cut.lowHz = value.getFloatValue(); }
        else if (cutArg.startsWith ("high:")) { mix.cut.mode = FrequencyCutSettings::Mode::highCut; mix.cut.highHz = value.getFloatValue(); }
        else if (cutArg.startsWith ("band:"))
        {
            mix.cut.mode = FrequencyCutSettings::Mode::bandCut;
            mix.cut.lowHz = value.upToFirstOccurrenceOf ("-", false, false).getFloatValue();
            mix.cut.highHz = value.fromFirstOccurrenceOf ("-", false, false).getFloatValue();
        }
        else return fail ("Bad --cut value. Use low:<hz>, high:<hz> or band:<lo>-<hi>");
    }

    // Load
    auto source = std::make_shared<SourceAudio>();
    auto r = AudioIO::loadFile (input, *source);

    if (r.failed())
        return fail (r.getErrorMessage());

    std::cout << input.getFileName() << ": " << source->getLengthSeconds() << " s, " << source->sampleRate << " Hz, "
              << source->originalNumChannels << " ch, " << source->bitsPerSample << "-bit" << std::endl;

    const bool frequencyCutOnly = removed.isEmpty() && mix.cut.enabled;
    StemSet stems;

    if (! frequencyCutOnly)
    {
        if (! ModelManager::isInstalled (engine))
            return fail ("The " + getEngineName (engine) + " model isn't installed. Run with --list-models for details.");

        SeparationContext ctx;
        ctx.numThreads = args.containsOption ("--threads") ? juce::jmax (1, optionValue (args, "--threads").getIntValue())
                                                          : SeparationJob::getRecommendedThreadCount (engine);
        int lastPercent = -1;
        ctx.reportProgress = [&lastPercent] (float p, const juce::String& stage)
        {
            const int pct = juce::roundToInt (p * 100.0f);
            if (pct != lastPercent)
            {
                lastPercent = pct;
                std::cerr << "\r" << stage << " " << pct << "%   " << std::flush;
            }
        };

        std::cout << "Separating with " << getEngineName (engine) << " on " << ctx.numThreads << " threads" << std::endl;
        r = SeparationJob::runSynchronously (*source, engine, stems, ctx);
        std::cerr << std::endl;

        if (r.failed())
            return fail (r.getErrorMessage());

        std::cout << "Separated in " << stems.secondsTaken << " s ("
                  << source->getLengthSeconds() / juce::jmax (0.001, stems.secondsTaken) << "x real time)" << std::endl;
    }

    if (removed.isEmpty() && ! mix.cut.enabled)
    {
        if (selected.isEmpty())
            for (auto& s : stems.stems)
                selected.add (s.type);

        juce::Array<juce::File> written;
        r = MixRenderer::exportStems (*source, stems, selected, outDir, exportOptions, written);

        if (r.failed())
            return fail (r.getErrorMessage());

        for (auto& f : written)
            std::cout << "Wrote " << f.getFullPathName() << std::endl;
    }
    else
    {
        for (auto t : removed)
            mix.stems[(size_t) toIndex (t)].muted = true;

        const auto* stemSet = frequencyCutOnly ? nullptr : &stems;
        const auto name = AudioIO::makeSafeFileName (input.getFileNameWithoutExtension()
                                                     + " (" + MixRenderer::describeMix (stemSet, mix, true) + ").wav");
        const auto file = AudioIO::getNonexistentSibling (outDir.getChildFile (name));

        r = MixRenderer::exportMix (*source, stemSet, mix, file, exportOptions);

        if (r.failed())
            return fail (r.getErrorMessage());

        std::cout << "Wrote " << file.getFullPathName() << std::endl;
    }

    return 0;
}
