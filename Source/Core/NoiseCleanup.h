#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <vector>

namespace stemsplitter
{

/** Settings for repairing a recording: steady background noise, mains hum, specific tones and rumble. */
struct CleanupSettings
{
    bool enabled = false;

    // Steady broadband noise: fans, air conditioning, hiss, room tone
    bool denoise = true;
    float reductionDb = 15.0f;     ///< how far the noise is turned down (3..40 dB)
    float sensitivity = 0.5f;      ///< 0..1: how aggressively quiet detail is treated as noise

    // Mains hum and its harmonics
    bool dehum = false;
    int humHz = 0;                 ///< 0 = automatic (detected), else 50 or 60
    int humHarmonics = 8;          ///< number of harmonics removed, including the fundamental

    // Specific tones (fan whine, buzz, whistle): notch filters
    std::vector<float> notchesHz;
    float notchWidth = 0.5f;       ///< 0 = very narrow, 1 = wide

    // Low-frequency rumble (handling noise, traffic, wind)
    bool rumble = false;
    float rumbleHz = 50.0f;

    bool hasAnyEffect() const noexcept
    {
        return enabled && (denoise || dehum || ! notchesHz.empty() || rumble);
    }

    /** Short description, e.g. "Denoise 15 dB + Hum 60 Hz + 2 notches". */
    juce::String describe (float detectedHumHz = 0.0f) const;

    bool operator== (const CleanupSettings& o) const noexcept;
    bool operator!= (const CleanupSettings& o) const noexcept { return ! (*this == o); }

    static constexpr int maxNotches = 16;
};

/** Average power per FFT bin of the noise, at the processing FFT size. */
struct NoiseProfile
{
    double sampleRate = 0.0;
    int fftSize = 0;
    std::vector<float> power;      ///< per bin, fftSize / 2 + 1 values
    bool learnedFromSelection = false;

    bool isValid() const noexcept { return fftSize > 0 && power.size() == (size_t) (fftSize / 2 + 1); }
};

/** What the analysis of a recording found: spectra for display, the automatic noise profile, any hum
    and persistent tones. */
struct CleanupAnalysis
{
    double sampleRate = 0.0;
    int displayFftSize = 0;
    std::vector<float> averageDb;  ///< long-term average spectrum (display resolution)
    std::vector<float> floorDb;    ///< steady noise floor: what's always there (display resolution)
    NoiseProfile autoProfile;      ///< from the quietest parts, at the processing FFT size
    float detectedHumHz = 0.0f;    ///< 50, 60 or 0
    std::vector<float> suggestedTonesHz;

    bool isValid() const noexcept { return displayFftSize > 0 && ! averageDb.empty(); }

    /** Display-resolution value at a frequency (linear interpolation between bins). */
    static float valueAt (const std::vector<float>& spectrumDb, int fftSize, double sampleRate, float hz);
};

namespace NoiseCleanup
{
    using ShouldCancel = std::function<bool()>;

    /** FFT size used for noise reduction at a given sample rate (~21 Hz resolution). */
    int getProcessingFftSize (double sampleRate);

    /** Analyses a recording (any channel count). Cheap: a few hundred ms for a song. */
    CleanupAnalysis analyse (const juce::AudioBuffer<float>& audio, double sampleRate, ShouldCancel cancel = {});

    /** Learns the noise from a region that contains only noise (e.g. the silence before a song starts). */
    NoiseProfile learnProfile (const juce::AudioBuffer<float>& audio, double sampleRate, int startSample, int numSamples);

    /** The mains hum frequency that will be removed (resolves "automatic"). */
    float resolveHumHz (const CleanupSettings& settings, const CleanupAnalysis& analysis);

    /** Applies the clean-up. Zero-phase: the output stays sample-aligned with the input.
        Returns an empty buffer if cancelled. */
    juce::AudioBuffer<float> process (const juce::AudioBuffer<float>& input, double sampleRate,
                                      const CleanupSettings& settings, const NoiseProfile& profile,
                                      float humHz, ShouldCancel cancel = {},
                                      std::function<void (float)> progress = {});

    /** Combined magnitude response (dB) of the hum/notch/rumble filters, for drawing. */
    float getFilterResponseDb (const CleanupSettings& settings, float humHz, double sampleRate, float hz);

    /** Snaps a clicked frequency to the strongest nearby peak of the noise floor. */
    float snapToPeak (const CleanupAnalysis& analysis, float hz, float searchOctaves = 0.08f);
}

} // namespace stemsplitter
