#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace stemsplitter
{

/** Audio decoded from a file, kept at its original sample rate. Immutable once loaded. */
struct SourceAudio
{
    juce::File file;
    juce::AudioBuffer<float> buffer;  ///< 1 or 2 channels (anything wider is folded down to stereo)
    double sampleRate = 44100.0;
    int originalNumChannels = 2;
    int bitsPerSample = 24;
    juce::String formatName;

    int getNumSamples() const noexcept     { return buffer.getNumSamples(); }
    double getLengthSeconds() const noexcept { return sampleRate > 0 ? getNumSamples() / sampleRate : 0.0; }
};

namespace AudioIO
{
    /** Formats that can be opened (WAV first; AIFF, FLAC, and on macOS anything CoreAudio reads). */
    juce::AudioFormatManager& getFormatManager();

    /** "*.wav;*.aif;..." pattern for file choosers. */
    juce::String getSupportedFilePattern();

    bool isSupportedFile (const juce::File& f);

    /** Decodes a file into memory. */
    juce::Result loadFile (const juce::File& file, SourceAudio& result,
                           std::function<bool (float)> progress = {});

    /** High-quality, zero-phase windowed-sinc sample-rate conversion (band-limited when
        downsampling). Returns a copy when the rates match. */
    juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& input,
                                       double sourceRate, double targetRate);

    /** Returns a copy with exactly `numChannels` channels (folds down or duplicates as needed). */
    juce::AudioBuffer<float> withChannelCount (const juce::AudioBuffer<float>& input, int numChannels);

    enum class BitDepth { int16 = 16, int24 = 24, float32 = 32 };

    struct WavOptions
    {
        BitDepth bitDepth = BitDepth::int24;
        double sampleRate = 44100.0;
    };

    /** Writes a WAV file. 16-bit output is TPDF-dithered. Integer formats are clipped at 0 dBFS. */
    juce::Result writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, const WavOptions& options);

    /** Returns `desired` if it doesn't exist, else "name (2).wav", "name (3).wav", ... */
    juce::File getNonexistentSibling (const juce::File& desired);

    /** Removes characters that aren't allowed in file names. */
    juce::String makeSafeFileName (const juce::String& name);
}

} // namespace stemsplitter
