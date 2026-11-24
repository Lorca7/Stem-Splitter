// stemsplit: command-line front end for the Stem Splitter engine.
//
//   stemsplit song.wav                          → song - Vocals.wav, song - Drums.wav, ...
//   stemsplit --engine 6 song.wav               → six stems (adds guitar + piano)
//   stemsplit --remove vocals song.wav          → song (No Vocals).wav
//   stemsplit --cut band:250-4000 song.wav      → song (Band Cut 250 Hz to 4 kHz).wav (no separation)
//   stemsplit --denoise 18 --dehum auto take.wav → take (Cleaned).wav (no separation)
//   stemsplit --analyse take.wav                → reports noise floor, hum and tones
//   stemsplit --selftest                        → runs the built-in DSP checks

#include "Core/MixRenderer.h"
#include "Core/NoiseCleanup.h"
#include "Core/ModelManager.h"
#include "Core/SeparationJob.h"

#include <algorithm>
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
        static const juce::StringArray valued { "--engine", "--out", "--stems", "--remove", "--cut", "--bits", "--rate", "--threads",
                                                "--denoise", "--sensitivity", "--dehum", "--notch", "--rumble", "--learn-noise" };
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

Clean-up (applied to the original and to every stem):
  --denoise <dB>           Reduce steady background noise (fans, AC, hiss) by this much, e.g. 15
  --sensitivity <0..1>     How aggressively quiet detail is treated as noise (default 0.5)
  --learn-noise <a-b>      Learn the noise from seconds a to b (default: automatic)
  --dehum <auto|50|60>     Remove mains hum and its harmonics
  --notch <hz,hz,...>      Remove specific tones (fan whine, buzz)
  --rumble <hz>            Remove rumble below this frequency
  --analyse                Show the noise floor, detected hum and suggested tones, then exit
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

        // 4. Clean-up: hum, steady noise, a fan whine and rumble
        {
            const double sr = 48000.0;
            const int n = (int) (sr * 8.0);
            const double twoPi = juce::MathConstants<double>::pi * 2.0;
            juce::AudioBuffer<float> music (2, n), noisy (2, n);
            juce::Random rng (99);

            for (int i = 0; i < n; ++i)
            {
                const double t = i / sr;
                // music: a 440 Hz note that comes and goes (on for 1 s, off for 1 s)
                const float note = ((int) t % 2 == 0) ? 0.3f * (float) std::sin (twoPi * 440.0 * t) : 0.0f;
                // 60 Hz hum with harmonics, a 2.2 kHz fan whine, broadband hiss and 18 Hz rumble
                float hum = 0.0f;
                for (int h = 1; h <= 4; ++h)
                    hum += 0.02f / (float) h * (float) std::sin (twoPi * 60.0 * h * t);
                const float whine = 0.006f * (float) std::sin (twoPi * 2200.0 * t);
                const float rumble = 0.05f * (float) std::sin (twoPi * 18.0 * t);

                for (int ch = 0; ch < 2; ++ch)
                {
                    const float hiss = 0.003f * (rng.nextFloat() * 2.0f - 1.0f);
                    music.setSample (ch, i, note);
                    noisy.setSample (ch, i, note + hum + whine + rumble + hiss);
                }
            }

            auto tone = [sr] (const juce::AudioBuffer<float>& b, double hz, int start, int len)
            {
                double re = 0, im = 0;
                for (int i = start; i < start + len; ++i)
                {
                    re += b.getSample (0, i) * std::cos (2.0 * juce::MathConstants<double>::pi * hz * i / sr);
                    im += b.getSample (0, i) * std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr);
                }
                return 2.0 * std::sqrt (re * re + im * im) / len;
            };

            const auto analysis = NoiseCleanup::analyse (noisy, sr);
            std::cout << "  clean-up: detected hum " << analysis.detectedHumHz << " Hz, tones:";
            for (auto hz : analysis.suggestedTonesHz)
                std::cout << " " << juce::String (hz, 1);
            std::cout << std::endl;

            if (! juce::approximatelyEqual (analysis.detectedHumHz, 60.0f))
                return fail ("hum detection");

            const bool foundWhine = std::any_of (analysis.suggestedTonesHz.begin(), analysis.suggestedTonesHz.end(),
                                                 [] (float hz) { return std::abs (hz - 2200.0f) < 5.0f; });
            const bool flaggedMusic = std::any_of (analysis.suggestedTonesHz.begin(), analysis.suggestedTonesHz.end(),
                                                   [] (float hz) { return std::abs (hz - 440.0f) < 10.0f; });
            if (! foundWhine || flaggedMusic)
                return fail ("tone detection should find the 2.2 kHz whine and leave the 440 Hz note alone");

            if (std::abs (NoiseCleanup::snapToPeak (analysis, 2150.0f) - 2200.0f) > 3.0f)
                return fail ("snapping a click to the whine peak");

            CleanupSettings c;
            c.enabled = true;
            c.denoise = true;
            c.reductionDb = 24.0f;
            c.dehum = true;
            c.notchesHz = { NoiseCleanup::snapToPeak (analysis, 2200.0f) };
            c.rumble = true;
            c.rumbleHz = 40.0f;

            const auto cleaned = NoiseCleanup::process (noisy, sr, c, analysis.autoProfile, NoiseCleanup::resolveHumHz (c, analysis));

            const int on = (int) sr / 2, off = (int) (sr * 1.25), len = (int) (sr * 0.5);   // inside a note / inside a gap
            auto reportDb = [] (double a, double b) { return juce::Decibels::gainToDecibels ((float) (b / a)); };

            const double humCut = reportDb (tone (noisy, 60, off, len), tone (cleaned, 60, off, len));
            const double harmonicCut = reportDb (tone (noisy, 180, off, len), tone (cleaned, 180, off, len));
            const double whineCut = reportDb (tone (noisy, 2200, off, len), tone (cleaned, 2200, off, len));
            const double rumbleCut = reportDb (tone (noisy, 18, 0, (int) sr * 4), tone (cleaned, 18, 0, (int) sr * 4));
            const double noteChange = reportDb (tone (noisy, 440, on, len), tone (cleaned, 440, on, len));

            juce::AudioBuffer<float> gapBefore (1, len), gapAfter (1, len), gapMusic (1, len);
            gapBefore.copyFrom (0, 0, noisy, 0, off, len);
            gapAfter.copyFrom (0, 0, cleaned, 0, off, len);
            const double floorCut = juce::Decibels::gainToDecibels ((float) (rms (gapAfter) / rms (gapBefore)));

            // Zero phase: in the middle of a note the cleaned signal lines up with the clean music
            juce::AudioBuffer<float> a (1, len), b (1, len);
            a.copyFrom (0, 0, music, 0, on, len);
            b.copyFrom (0, 0, cleaned, 0, on, len);
            const double alignmentErr = juce::Decibels::gainToDecibels ((float) (rmsDifference (a, b) / rms (a)));

            std::cout << "  clean-up: hum " << humCut << " dB, 3rd harmonic " << harmonicCut << " dB, whine " << whineCut
                      << " dB, rumble " << rumbleCut << " dB, noise in gaps " << floorCut << " dB, note " << noteChange
                      << " dB, residual vs clean music " << alignmentErr << " dB" << std::endl;

            if (humCut > -30 || harmonicCut > -30 || whineCut > -30 || rumbleCut > -20 || floorCut > -15
                || std::abs (noteChange) > 0.5 || alignmentErr > -25)
                return fail ("clean-up");

            CleanupSettings off2 = c;
            off2.enabled = false;
            if (rmsDifference (NoiseCleanup::process (noisy, sr, off2, analysis.autoProfile, 60.0f), noisy) > 0.0)
                return fail ("disabled clean-up must not alter the audio");

            // A learned profile (from a gap) works as well as the automatic one
            const auto learned = NoiseCleanup::learnProfile (noisy, sr, (int) (sr * 1.1), (int) (sr * 0.8));
            CleanupSettings onlyDenoise;
            onlyDenoise.enabled = true;
            onlyDenoise.reductionDb = 24.0f;
            const auto denoised = NoiseCleanup::process (noisy, sr, onlyDenoise, learned, 60.0f);
            gapAfter.copyFrom (0, 0, denoised, 0, (int) (sr * 3.25), len);
            gapBefore.copyFrom (0, 0, noisy, 0, (int) (sr * 3.25), len);
            const double learnedCut = juce::Decibels::gainToDecibels ((float) (rms (gapAfter) / rms (gapBefore)));
            std::cout << "  clean-up: learned profile reduces the gaps by " << learnedCut << " dB" << std::endl;

            if (! learned.learnedFromSelection || learnedCut > -15)
                return fail ("learned noise profile");
        }

        // 5. WAV round trip at each bit depth
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

    // Clean-up options
    CleanupSettings cleanup;
    const auto denoiseArg = optionValue (args, "--denoise");
    const auto dehumArg = optionValue (args, "--dehum").toLowerCase();
    const auto notchArg = optionValue (args, "--notch");
    const auto rumbleArg = optionValue (args, "--rumble");

    cleanup.denoise = denoiseArg.isNotEmpty();
    cleanup.reductionDb = denoiseArg.isNotEmpty() ? juce::jlimit (3.0f, 40.0f, denoiseArg.getFloatValue()) : 15.0f;
    if (args.containsOption ("--sensitivity"))
        cleanup.sensitivity = juce::jlimit (0.0f, 1.0f, optionValue (args, "--sensitivity").getFloatValue());
    cleanup.dehum = dehumArg.isNotEmpty();
    cleanup.humHz = dehumArg == "50" ? 50 : dehumArg == "60" ? 60 : 0;
    for (auto& t : juce::StringArray::fromTokens (notchArg, ",", ""))
        if (t.getFloatValue() > 0.0f)
            cleanup.notchesHz.push_back (t.getFloatValue());
    cleanup.rumble = rumbleArg.isNotEmpty();
    if (cleanup.rumble)
        cleanup.rumbleHz = juce::jlimit (20.0f, 200.0f, rumbleArg.getFloatValue());
    cleanup.enabled = cleanup.denoise || cleanup.dehum || ! cleanup.notchesHz.empty() || cleanup.rumble;

    CleanupAnalysis analysis;
    NoiseProfile profile;

    if (cleanup.enabled || args.containsOption ("--analyse|--analyze"))
    {
        analysis = NoiseCleanup::analyse (source->buffer, source->sampleRate);
        profile = analysis.autoProfile;

        const auto learnArg = optionValue (args, "--learn-noise");

        if (learnArg.isNotEmpty())
        {
            const double a0 = learnArg.upToFirstOccurrenceOf ("-", false, false).getDoubleValue();
            const double a1 = learnArg.fromFirstOccurrenceOf ("-", false, false).getDoubleValue();
            profile = NoiseCleanup::learnProfile (source->buffer, source->sampleRate, (int) (a0 * source->sampleRate),
                                                  (int) ((a1 - a0) * source->sampleRate));
        }

        if (args.containsOption ("--analyse|--analyze"))
        {
            std::cout << "Detected mains hum: " << (analysis.detectedHumHz > 0 ? juce::String (analysis.detectedHumHz) + " Hz" : juce::String ("none")) << std::endl;
            std::cout << "Suggested tones to notch:";
            for (auto hz : analysis.suggestedTonesHz)
                std::cout << " " << juce::String (hz, 1) << " Hz";
            std::cout << (analysis.suggestedTonesHz.empty() ? " none" : "") << std::endl;
            std::cout << "Noise floor (dBFS):";
            for (float hz : { 50.0f, 100.0f, 500.0f, 1000.0f, 4000.0f, 10000.0f })
                std::cout << "  " << hz << " Hz " << juce::String (CleanupAnalysis::valueAt (analysis.floorDb, analysis.displayFftSize, analysis.sampleRate, hz), 1);
            std::cout << std::endl;
            return 0;
        }
    }

    const bool wantsStems = args.containsOption ("--engine") || args.containsOption ("--stems");
    const bool noSeparation = removed.isEmpty() && (mix.cut.enabled || cleanup.enabled) && ! wantsStems;
    StemSet stems;

    if (! noSeparation)
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

    // Clean-up is applied to the original and to every stem
    if (cleanup.enabled)
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const float humHz = NoiseCleanup::resolveHumHz (cleanup, analysis);
        source->buffer = NoiseCleanup::process (source->buffer, source->sampleRate, cleanup, profile, humHz);

        for (auto& s : stems.stems)
            s.buffer = NoiseCleanup::process (s.buffer, source->sampleRate, cleanup, profile, humHz);

        std::cout << "Cleaned up (" << cleanup.describe (humHz) << ") in "
                  << juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 2) << " s" << std::endl;
    }

    if (removed.isEmpty() && ! noSeparation && ! mix.cut.enabled)
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

        const auto* stemSet = noSeparation ? nullptr : &stems;
        auto description = MixRenderer::describeMix (stemSet, mix, true);

        if (cleanup.enabled)
            description = description == "Mix" ? juce::String ("Cleaned") : description + " + Cleaned";

        const auto name = AudioIO::makeSafeFileName (input.getFileNameWithoutExtension() + " (" + description + ").wav");
        const auto file = AudioIO::getNonexistentSibling (outDir.getChildFile (name));

        r = MixRenderer::exportMix (*source, stemSet, mix, file, exportOptions);

        if (r.failed())
            return fail (r.getErrorMessage());

        std::cout << "Wrote " << file.getFullPathName() << std::endl;
    }

    return 0;
}
