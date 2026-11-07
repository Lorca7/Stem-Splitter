#include "AudioIO.h"

#include <cmath>
#include <random>
#include <thread>

namespace stemsplitter::AudioIO
{

juce::AudioFormatManager& getFormatManager()
{
    static juce::AudioFormatManager manager;
    static const bool initialised = [] { manager.registerBasicFormats(); return true; }();  // WAV, AIFF, FLAC, Ogg (+ CoreAudio on macOS)
    juce::ignoreUnused (initialised);
    return manager;
}

juce::String getSupportedFilePattern()
{
    return "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.caf;*.ogg";
}

bool isSupportedFile (const juce::File& f)
{
    return f.existsAsFile() && f.hasFileExtension ("wav;wave;aif;aiff;flac;mp3;m4a;caf;ogg");
}

juce::Result loadFile (const juce::File& file, SourceAudio& result, std::function<bool (float)> progress)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("The file \"" + file.getFileName() + "\" could not be found.");

    std::unique_ptr<juce::AudioFormatReader> reader (getFormatManager().createReaderFor (file));

    if (reader == nullptr)
        return juce::Result::fail ("\"" + file.getFileName() + "\" isn't an audio file that can be opened.");

    const auto numSamples = reader->lengthInSamples;
    const auto numChannels = (int) reader->numChannels;

    if (numSamples <= 0 || numChannels <= 0)
        return juce::Result::fail ("\"" + file.getFileName() + "\" contains no audio.");

    // 2^31 samples is ~13 hours at 44.1 kHz; keep well clear of int overflow in AudioBuffer.
    if (numSamples > (juce::int64) std::numeric_limits<int>::max() / 2)
        return juce::Result::fail ("\"" + file.getFileName() + "\" is too long.");

    const int length = (int) numSamples;
    const int outChannels = juce::jmin (2, numChannels);

    juce::AudioBuffer<float> raw (numChannels, length);
    constexpr int chunk = 1 << 18;

    for (int pos = 0; pos < length; pos += chunk)
    {
        const int n = juce::jmin (chunk, length - pos);

        if (! reader->read (&raw, pos, n, pos, true, true))
            return juce::Result::fail ("Failed to read audio from \"" + file.getFileName() + "\".");

        if (progress && ! progress ((float) (pos + n) / (float) length))
            return juce::Result::fail ("Cancelled");
    }

    result.file = file;
    result.sampleRate = reader->sampleRate;
    result.originalNumChannels = numChannels;
    result.bitsPerSample = (int) reader->bitsPerSample;
    result.formatName = reader->getFormatName();
    result.buffer = withChannelCount (raw, outChannels);
    return juce::Result::ok();
}

juce::AudioBuffer<float> withChannelCount (const juce::AudioBuffer<float>& input, int numChannels)
{
    const int inChannels = input.getNumChannels();
    const int length = input.getNumSamples();

    if (inChannels == numChannels)
        return input;

    juce::AudioBuffer<float> out (numChannels, length);
    out.clear();

    if (inChannels == 0)
        return out;

    if (inChannels == 1)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            out.copyFrom (ch, 0, input, 0, 0, length);
    }
    else if (numChannels == 1)
    {
        const float g = 1.0f / (float) inChannels;

        for (int ch = 0; ch < inChannels; ++ch)
            out.addFrom (0, 0, input, ch, 0, length, g);
    }
    else
    {
        // Fold down multichannel: odd channels left, even channels right (L R C LFE Ls Rs → L, R),
        // with centre/LFE shared between both sides at -3 dB.
        for (int ch = 0; ch < inChannels; ++ch)
        {
            if (inChannels > 2 && (ch == 2 || ch == 3))
            {
                for (int o = 0; o < numChannels; ++o)
                    out.addFrom (o, 0, input, ch, 0, length, 0.7071f);
            }
            else
            {
                out.addFrom (ch % numChannels, 0, input, ch, 0, length, inChannels > 2 ? 0.7071f : 1.0f);
            }
        }
    }

    return out;
}

//==============================================================================
namespace
{
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        const double q = x * x / 4.0;

        for (int k = 1; k < 64; ++k)
        {
            term *= q / ((double) k * (double) k);
            sum += term;

            if (term < sum * 1.0e-12)
                break;
        }

        return sum;
    }

    /** Kaiser-windowed sinc kernel, tabulated for fast lookup with linear interpolation. */
    struct SincKernel
    {
        SincKernel (double cutoff, int zeroCrossings)
            : fc (cutoff), halfWidth ((double) zeroCrossings / cutoff)
        {
            constexpr double beta = 9.0;
            const int size = (int) std::ceil (halfWidth * resolution) + 2;
            table.resize ((size_t) size);
            const double i0Beta = besselI0 (beta);

            for (int i = 0; i < size; ++i)
            {
                const double x = (double) i / resolution;

                if (x >= halfWidth)
                {
                    table[(size_t) i] = 0.0f;
                    continue;
                }

                const double r = x / halfWidth;
                const double window = besselI0 (beta * std::sqrt (1.0 - r * r)) / i0Beta;
                const double arg = juce::MathConstants<double>::pi * fc * x;
                const double sinc = i == 0 ? 1.0 : std::sin (arg) / arg;
                table[(size_t) i] = (float) (fc * sinc * window);
            }
        }

        float operator() (double x) const noexcept
        {
            const double pos = std::abs (x) * resolution;
            const auto i = (size_t) pos;

            if (i + 1 >= table.size())
                return 0.0f;

            const float frac = (float) (pos - (double) i);
            return table[i] + frac * (table[i + 1] - table[i]);
        }

        static constexpr double resolution = 512.0;
        double fc, halfWidth;
        std::vector<float> table;
    };
}

juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& input, double sourceRate, double targetRate)
{
    if (sourceRate <= 0 || targetRate <= 0 || std::abs (sourceRate - targetRate) < 1.0e-6)
        return input;

    const int inLength = input.getNumSamples();
    const int numChannels = input.getNumChannels();
    const double step = sourceRate / targetRate;  // input samples per output sample
    const int outLength = (int) std::llround ((double) inLength / step);

    juce::AudioBuffer<float> output (numChannels, juce::jmax (0, outLength));

    if (outLength <= 0 || inLength == 0)
        return output;

    // Pass band to ~95% of the lower Nyquist frequency: full audio band, no aliasing.
    const double cutoff = juce::jmin (1.0, 1.0 / step) * 0.95;
    const SincKernel kernel (cutoff, 32);
    const double halfWidth = kernel.halfWidth;

    auto processChannel = [&] (int ch)
    {
        const float* in = input.getReadPointer (ch);
        float* out = output.getWritePointer (ch);

        for (int n = 0; n < outLength; ++n)
        {
            const double centre = (double) n * step;
            const int first = juce::jmax (0, (int) std::ceil (centre - halfWidth));
            const int last = juce::jmin (inLength - 1, (int) std::floor (centre + halfWidth));

            float sum = 0.0f;

            for (int k = first; k <= last; ++k)
                sum += in[k] * kernel (centre - (double) k);

            out[n] = sum;
        }
    };

    std::vector<std::thread> threads;

    for (int ch = 1; ch < numChannels; ++ch)
        threads.emplace_back (processChannel, ch);

    processChannel (0);

    for (auto& t : threads)
        t.join();

    return output;
}

//==============================================================================
juce::Result writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, const WavOptions& options)
{
    const int numChannels = audio.getNumChannels();
    const int length = audio.getNumSamples();
    const int bits = (int) options.bitDepth;

    if (numChannels <= 0)
        return juce::Result::fail ("Nothing to write.");

    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("Couldn't create the folder " + file.getParentDirectory().getFullPathName());

    const juce::File temp = file.getSiblingFile ("." + file.getFileName() + ".partial");
    temp.deleteFile();

    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (temp);

        if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
            return juce::Result::fail ("Couldn't write to " + file.getFullPathName() + ". Check the folder permissions.");

        juce::WavAudioFormat wav;

        const auto writerOptions = juce::AudioFormatWriterOptions{}
                                       .withSampleRate (options.sampleRate)
                                       .withNumChannels (numChannels)
                                       .withBitsPerSample (bits)
                                       .withSampleFormat (options.bitDepth == BitDepth::float32
                                                              ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                              : juce::AudioFormatWriterOptions::SampleFormat::integral)
                                       .withMetadata (juce::WavAudioFormat::bwavOriginator, "Stem Splitter");

        auto writer = wav.createWriterFor (stream, writerOptions);

        if (writer == nullptr)
            return juce::Result::fail ("This WAV format isn't supported (" + juce::String (bits) + "-bit, "
                                       + juce::String (options.sampleRate) + " Hz).");

        constexpr int blockSize = 65536;
        juce::AudioBuffer<float> block (numChannels, blockSize);
        std::minstd_rand rng (0x5eed);
        std::uniform_real_distribution<float> uniform (-0.5f, 0.5f);
        const float lsb = 1.0f / 32768.0f;

        for (int pos = 0; pos < length; pos += blockSize)
        {
            const int n = juce::jmin (blockSize, length - pos);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* src = audio.getReadPointer (ch, pos);
                float* dst = block.getWritePointer (ch);

                if (options.bitDepth == BitDepth::float32)
                {
                    juce::FloatVectorOperations::copy (dst, src, n);
                }
                else
                {
                    const bool dither = options.bitDepth == BitDepth::int16;

                    for (int i = 0; i < n; ++i)
                    {
                        float s = src[i];

                        if (dither)
                            s += (uniform (rng) + uniform (rng)) * lsb;  // TPDF, ±1 LSB

                        dst[i] = juce::jlimit (-1.0f, 1.0f, s);
                    }
                }
            }

            if (! writer->writeFromAudioSampleBuffer (block, 0, n))
            {
                writer.reset();
                temp.deleteFile();
                return juce::Result::fail ("Writing " + file.getFileName() + " failed. Is the disk full?");
            }
        }
    }

    if (! temp.moveFileTo (file))
    {
        temp.deleteFile();
        return juce::Result::fail ("Couldn't save " + file.getFullPathName());
    }

    return juce::Result::ok();
}

juce::File getNonexistentSibling (const juce::File& desired)
{
    if (! desired.exists())
        return desired;

    const auto base = desired.getFileNameWithoutExtension();
    const auto ext = desired.getFileExtension();

    for (int i = 2; i < 10000; ++i)
    {
        auto candidate = desired.getSiblingFile (base + " (" + juce::String (i) + ")" + ext);

        if (! candidate.exists())
            return candidate;
    }

    return desired.getNonexistentSibling();
}

juce::String makeSafeFileName (const juce::String& name)
{
    auto safe = juce::File::createLegalFileName (name.trim());
    safe = safe.replaceCharacter (':', '-').replaceCharacter ('/', '-');
    return safe.isEmpty() ? juce::String ("Untitled") : safe;
}

} // namespace stemsplitter::AudioIO
