#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "AudioDownmix.hpp"

// Pure, CPU-only PCM analysis helpers for the editor's audio preview (VK-1510):
// generalized waveform bucketing (zoom/scroll), per-channel level metering (RMS +
// peak), and a hand-written radix-2 FFT spectrum. Header-only inline so both the
// editor window (editor/windows/preview/AudioPreviewWindow.cpp) and the unit tests
// (tests/) can use it without linking ImGui, OpenAL, or the audio DLL. Kept next to
// AudioDownmix.hpp / VfAudioHeader.hpp / AudioResource.hpp (the audio-IO/PCM home).
//
// All input PCM is frame-interleaved int16 (resource::AudioData::data), size ==
// frames * channels; index units below are per-channel FRAMES. Samples normalize to
// [-1,1] via sample / 32768.0f (note: +32767 -> 0.999969, -32768 -> -1.0 exactly).

namespace resource
{
    inline constexpr std::size_t kFftSize      = 2048;             // hand-written radix-2 window size
    inline constexpr std::size_t kSpectrumBins = kFftSize / 2;     // 1024 single-sided bins
    inline constexpr double      kPi           = 3.14159265358979323846; // avoid M_PI (absent on MSVC)
    inline constexpr float       kInt16Norm    = 32768.0f;         // sample / 32768.0f

    // One waveform bucket: min/max of the samples it spans, normalized. Seeded at 0
    // so minVal <= 0 <= maxVal always (zero-straddle), matching the preview's
    // centre-line drawing. Home of the type shared by the window + tests.
    struct WaveformBucket
    {
        float minVal = 0.0f;
        float maxVal = 0.0f;
    };

    // Per-channel level readout over an analysis window. Both normalized [0,1].
    // Peak-HOLD decay is UI state and lives in the caller, not here.
    struct ChannelLevel
    {
        float rms  = 0.0f;
        float peak = 0.0f; // instantaneous |sample| peak
    };

    // VK-1514: compact whole-asset RMS envelopes for estimated bus metering. Code 0 is
    // reserved for exact silence; positive codes cover the display's -60..0 dB range.
    inline constexpr std::size_t kEnvelopeWindowsPerSecond = 100;
    inline constexpr float       kEnvelopeFloorDb          = -60.0f;

    inline uint8_t encodeEnvelopeDb(float rms)
    {
        if (!std::isfinite(rms) || rms <= 0.0f)
            return 0;

        const float db = std::clamp(20.0f * std::log10(rms), kEnvelopeFloorDb, 0.0f);
        const float normalized = (db - kEnvelopeFloorDb) / -kEnvelopeFloorDb;
        return static_cast<uint8_t>(1 + std::lround(normalized * 254.0f));
    }

    inline float decodeEnvelopeDb(uint8_t code)
    {
        static const std::array<float, 256> lut = []
        {
            std::array<float, 256> values{};
            for (std::size_t i = 1; i < values.size(); ++i)
            {
                const float db = kEnvelopeFloorDb
                    + static_cast<float>(i - 1) * (-kEnvelopeFloorDb / 254.0f);
                values[i] = std::pow(10.0f, db / 20.0f);
            }
            return values;
        }();
        return lut[code];
    }

    // Builds 10 ms power-RMS windows over all complete interleaved frames. Channel
    // powers are averaged rather than downmixed, so anti-phase stereo cannot cancel.
    inline std::vector<uint8_t> buildRmsEnvelope(std::span<const short> interleaved,
                                                  uint32_t channels,
                                                  uint32_t sampleRate)
    {
        if (interleaved.empty() || channels == 0 || sampleRate == 0)
            return {};

        const std::size_t totalFrames = interleaved.size() / channels;
        if (totalFrames == 0)
            return {};

        const std::uint64_t scaledFrames = static_cast<std::uint64_t>(totalFrames)
            * static_cast<std::uint64_t>(kEnvelopeWindowsPerSecond);
        const std::size_t windowCount = static_cast<std::size_t>(
            (scaledFrames + sampleRate - 1u) / sampleRate);

        std::vector<uint8_t> envelope(windowCount, 0);
        for (std::size_t window = 0; window < windowCount; ++window)
        {
            const std::size_t beginFrame = std::min<std::size_t>(
                totalFrames,
                static_cast<std::size_t>((static_cast<std::uint64_t>(window) * sampleRate)
                                         / kEnvelopeWindowsPerSecond));
            const std::size_t endFrame = std::min<std::size_t>(
                totalFrames,
                static_cast<std::size_t>((static_cast<std::uint64_t>(window + 1) * sampleRate)
                                         / kEnvelopeWindowsPerSecond));
            if (endFrame <= beginFrame)
                continue;

            double sumSquares = 0.0;
            for (std::size_t frame = beginFrame; frame < endFrame; ++frame)
            {
                const std::size_t base = frame * channels;
                for (uint32_t channel = 0; channel < channels; ++channel)
                {
                    const double sample = static_cast<double>(interleaved[base + channel])
                        / static_cast<double>(kInt16Norm);
                    sumSquares += sample * sample;
                }
            }

            const double sampleCount = static_cast<double>(endFrame - beginFrame)
                * static_cast<double>(channels);
            envelope[window] = encodeEnvelopeDb(
                static_cast<float>(std::sqrt(sumSquares / sampleCount)));
        }
        return envelope;
    }

    inline float sampleEnvelope(std::span<const uint8_t> envelope, float seconds)
    {
        if (envelope.empty() || !std::isfinite(seconds))
            return 0.0f;

        seconds = std::max(seconds, 0.0f);
        const double scaled = static_cast<double>(seconds)
            * static_cast<double>(kEnvelopeWindowsPerSecond);
        if (scaled >= static_cast<double>(envelope.size()))
            return decodeEnvelopeDb(envelope.back());
        return decodeEnvelopeDb(envelope[static_cast<std::size_t>(scaled)]);
    }

    enum class WindowFn
    {
        Rect,
        Hann
    };

    // ------------------------------------------------------------------
    // 1. Generalized waveform bucketing.
    //
    // Buckets the per-channel frame range [startFrame, endFrame) into exactly
    // bucketCount min/max pairs, min/max MERGED across all channels (single-trace
    // waveform). Index units are FRAMES. Generalizes the preview's old fixed
    // full-file 1024-bucket generateWaveformCache to an arbitrary sub-range for
    // zoom/scroll.
    //
    //   endFrame is clamped to the available frame count; startFrame is clamped to
    //   endFrame. A proportional split covers the whole range with no tail-drop; when
    //   bucketCount > rangeFrames, the surplus buckets stay {0,0}. Empty data,
    //   channels == 0, or bucketCount == 0 return an empty vector.
    inline std::vector<WaveformBucket> computeWaveformBuckets(
        std::span<const short> interleaved,
        uint32_t               channels,
        std::size_t            startFrame,
        std::size_t            endFrame,
        std::size_t            bucketCount)
    {
        if (interleaved.empty() || channels == 0 || bucketCount == 0)
        {
            return {};
        }

        const std::size_t totalFrames = interleaved.size() / channels;
        if (endFrame > totalFrames)
        {
            endFrame = totalFrames;
        }
        if (startFrame > endFrame)
        {
            startFrame = endFrame;
        }
        const std::size_t rangeFrames = endFrame - startFrame;

        std::vector<WaveformBucket> out(bucketCount); // default {0,0}
        if (rangeFrames == 0)
        {
            return out; // stable count for the UI even for an empty range
        }

        for (std::size_t i = 0; i < bucketCount; ++i)
        {
            const std::size_t bs = startFrame + (i * rangeFrames) / bucketCount;
            const std::size_t be = startFrame + ((i + 1) * rangeFrames) / bucketCount;

            short mn = 0;
            short mx = 0;
            for (std::size_t f = bs; f < be; ++f)
            {
                const std::size_t base = f * channels;
                for (uint32_t c = 0; c < channels; ++c)
                {
                    const short s = interleaved[base + c];
                    mn = std::min(mn, s);
                    mx = std::max(mx, s);
                }
            }

            out[i].minVal = static_cast<float>(mn) / kInt16Norm;
            out[i].maxVal = static_cast<float>(mx) / kInt16Norm;
        }

        return out;
    }

    // ------------------------------------------------------------------
    // 2. Per-channel window levels (RMS + instantaneous peak).
    //
    // Computes RMS and peak over windowFrames frames CENTERED on centerFrame, one
    // ChannelLevel per channel. The window is clamped to the available data (so it
    // is correct at either end, dividing by the actual scanned frame count) and never
    // reads out of bounds. channels == 0 returns empty; a zero window or a window
    // entirely off the data returns all-zero levels.
    inline std::vector<ChannelLevel> computeWindowLevels(
        std::span<const short> interleaved,
        uint32_t               channels,
        std::size_t            centerFrame,
        std::size_t            windowFrames)
    {
        if (channels == 0)
        {
            return {};
        }

        std::vector<ChannelLevel> out(channels); // default {0,0}
        const std::size_t totalFrames = interleaved.size() / channels;
        if (totalFrames == 0 || windowFrames == 0)
        {
            return out;
        }

        // Signed math so a window running off the start doesn't underflow size_t.
        const std::int64_t half = static_cast<std::int64_t>(windowFrames) / 2;
        const std::int64_t d0   = static_cast<std::int64_t>(centerFrame) - half;
        const std::int64_t d1   = d0 + static_cast<std::int64_t>(windowFrames);
        const std::size_t  lo   = static_cast<std::size_t>(
            std::clamp<std::int64_t>(d0, 0, static_cast<std::int64_t>(totalFrames)));
        const std::size_t  hi   = static_cast<std::size_t>(
            std::clamp<std::int64_t>(d1, 0, static_cast<std::int64_t>(totalFrames)));
        const std::size_t  count = hi - lo;
        if (count == 0)
        {
            return out; // window entirely off the data
        }

        for (uint32_t c = 0; c < channels; ++c)
        {
            double sumSq   = 0.0;
            float  absPeak = 0.0f;
            for (std::size_t f = lo; f < hi; ++f)
            {
                const float s = static_cast<float>(interleaved[f * channels + c]) / kInt16Norm;
                sumSq += static_cast<double>(s) * static_cast<double>(s);
                absPeak = std::max(absPeak, std::fabs(s));
            }
            out[c].rms  = static_cast<float>(std::sqrt(sumSq / static_cast<double>(count)));
            out[c].peak = absPeak;
        }

        return out;
    }

    // ------------------------------------------------------------------
    // 3a. In-place forward radix-2 Cooley-Tukey FFT.
    //
    // a.size() MUST be a power of two (>= 2); otherwise this is a no-op. Twiddles are
    // recomputed per index in double to avoid accumulated float drift over the stages.
    inline void fftRadix2(std::span<std::complex<float>> a)
    {
        const std::size_t n = a.size();
        if (n < 2 || (n & (n - 1)) != 0)
        {
            return; // not a power of two
        }

        // Bit-reversal permutation.
        for (std::size_t i = 1, j = 0; i < n; ++i)
        {
            std::size_t bit = n >> 1;
            for (; (j & bit) != 0; bit >>= 1)
            {
                j ^= bit;
            }
            j ^= bit;
            if (i < j)
            {
                std::swap(a[i], a[j]);
            }
        }

        // Danielson-Lanczos butterflies.
        for (std::size_t len = 2; len <= n; len <<= 1)
        {
            const std::size_t half = len >> 1;
            for (std::size_t start = 0; start < n; start += len)
            {
                for (std::size_t k = 0; k < half; ++k)
                {
                    const double ang = -2.0 * kPi * static_cast<double>(k) / static_cast<double>(len);
                    const std::complex<float> w(static_cast<float>(std::cos(ang)),
                                                static_cast<float>(std::sin(ang)));
                    const std::complex<float> u = a[start + k];
                    const std::complex<float> v = a[start + k + half] * w;
                    a[start + k]        = u + v;
                    a[start + k + half] = u - v;
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // 3b. Spectrum from PCM at the playhead.
    //
    // Reads kFftSize frames starting at startFrame (start-based; the UI centers by
    // passing startFrame = playhead - kFftSize/2, clamped), mono-downmixes multichannel
    // input, applies the window, FFTs, and returns kSpectrumBins amplitude-normalized
    // single-sided magnitudes. Bin k center frequency = k * sampleRate / kFftSize.
    // Frames past the end (or a startFrame beyond the data) are zero-padded.
    //
    // Normalization: mag[0] = |X0|/N, mag[k] = 2|Xk|/N. With WindowFn::Rect a full-scale
    // tone at an exact bin yields mag == its amplitude (used by the unit tests); Hann is
    // the default for the UI (smoother, coherent gain 0.5).
    inline std::vector<float> computeSpectrum(
        std::span<const short> interleaved,
        uint32_t               channels,
        std::size_t            startFrame,
        WindowFn               window = WindowFn::Hann)
    {
        std::vector<float> mags(kSpectrumBins, 0.0f);
        if (channels == 0 || interleaved.empty())
        {
            return mags;
        }

        // Mono source: a direct view when already mono, else a downmixed buffer that
        // must outlive the fill loop below.
        std::vector<short>     monoStorage;
        std::span<const short> mono;
        if (channels == 1)
        {
            mono = interleaved;
        }
        else
        {
            monoStorage = downmixToMono(interleaved, channels);
            mono        = monoStorage;
        }
        const std::size_t monoFrames = mono.size();

        std::vector<std::complex<float>> buf(kFftSize);
        for (std::size_t i = 0; i < kFftSize; ++i)
        {
            const std::size_t frame = startFrame + i;
            float x = (frame < monoFrames) ? static_cast<float>(mono[frame]) / kInt16Norm : 0.0f;
            if (window == WindowFn::Hann)
            {
                const float w = 0.5f * (1.0f - static_cast<float>(std::cos(
                                                   2.0 * kPi * static_cast<double>(i) /
                                                   static_cast<double>(kFftSize))));
                x *= w;
            }
            buf[i] = std::complex<float>(x, 0.0f);
        }

        fftRadix2(buf);

        mags[0] = std::abs(buf[0]) / static_cast<float>(kFftSize);
        for (std::size_t k = 1; k < kSpectrumBins; ++k)
        {
            mags[k] = 2.0f * std::abs(buf[k]) / static_cast<float>(kFftSize);
        }

        return mags;
    }

    // ------------------------------------------------------------------
    // 3c. Log-frequency bar binning.
    //
    // Groups linear magnitude bins (from computeSpectrum) into barCount log-spaced
    // [minHz, maxHz) bands; each bar is the MAX magnitude across its bin range
    // (spectrum-analyzer look). Bands spanning < 1 bin use the nearest bin. Keeping
    // this out of the UI makes the log mapping unit-testable.
    inline std::vector<float> binSpectrumLog(
        std::span<const float> magnitudes,
        uint32_t               sampleRate,
        std::size_t            barCount,
        float                  minHz = 20.0f,
        float                  maxHz = 20000.0f)
    {
        std::vector<float> bars(barCount, 0.0f);
        if (magnitudes.empty() || barCount == 0 || sampleRate == 0)
        {
            return bars;
        }

        const std::size_t numBins = magnitudes.size();
        const float       nyquist = static_cast<float>(sampleRate) * 0.5f;
        if (maxHz > nyquist)
        {
            maxHz = nyquist;
        }
        if (minHz < 1.0f)
        {
            minHz = 1.0f;
        }
        if (maxHz <= minHz)
        {
            return bars;
        }

        // bin index for frequency f == f * kFftSize / sampleRate == f * (2*numBins)/sampleRate.
        const float binsPerHz = static_cast<float>(2 * numBins) / static_cast<float>(sampleRate);
        const float ratio     = maxHz / minHz;

        for (std::size_t b = 0; b < barCount; ++b)
        {
            const float fLo = minHz * std::pow(ratio, static_cast<float>(b) / static_cast<float>(barCount));
            const float fHi = minHz * std::pow(ratio, static_cast<float>(b + 1) / static_cast<float>(barCount));

            std::size_t kLo = static_cast<std::size_t>(std::lround(fLo * binsPerHz));
            std::size_t kHi = static_cast<std::size_t>(std::lround(fHi * binsPerHz));
            if (kLo >= numBins)
            {
                kLo = numBins - 1;
            }
            if (kHi > numBins)
            {
                kHi = numBins;
            }
            if (kHi <= kLo)
            {
                kHi = kLo + 1; // at least the nearest bin
            }

            float peak = 0.0f;
            for (std::size_t k = kLo; k < kHi && k < numBins; ++k)
            {
                peak = std::max(peak, magnitudes[k]);
            }
            bars[b] = peak;
        }

        return bars;
    }
}
