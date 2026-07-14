#include <doctest.h>
#include <resource/AudioAnalysis.hpp>
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

// ============================================================
// VK-1510: audio preview level meters, waveform zoom/scroll, spectrum strip.
// resource::AudioAnalysis is the pure, CPU-only, header-only core: waveform
// bucketing over an arbitrary frame range, per-channel RMS/peak metering, and a
// hand-written radix-2 FFT spectrum. No OpenAL device, no ImGui, no editor module.
// ============================================================

using resource::ChannelLevel;
using resource::WaveformBucket;
using resource::WindowFn;

namespace
{
    constexpr float kNorm = resource::kInt16Norm; // 32768.0f
}

TEST_SUITE("AudioAnalysis")
{
    // ---------- computeWaveformBuckets ----------

    TEST_CASE("bucketing: whole mono buffer exact min/max")
    {
        const std::vector<short> in{10, 20, -30, 40};
        const auto b = resource::computeWaveformBuckets(in, 1, 0, 4, 1);
        REQUIRE(b.size() == 1);
        CHECK(b[0].minVal == doctest::Approx(-30.0f / kNorm));
        CHECK(b[0].maxVal == doctest::Approx(40.0f / kNorm));
    }

    TEST_CASE("bucketing: zero-straddle keeps min<=0<=max for all-positive input")
    {
        const std::vector<short> in{10, 20, 30};
        const auto b = resource::computeWaveformBuckets(in, 1, 0, 3, 1);
        REQUIRE(b.size() == 1);
        CHECK(b[0].minVal == 0.0f); // seeded at 0, never goes positive
        CHECK(b[0].maxVal == doctest::Approx(30.0f / kNorm));
    }

    TEST_CASE("bucketing: sub-range [1,4) scans only the interior frames")
    {
        const std::vector<short> in{0, 100, 0, -100, 50, 50};
        const auto b = resource::computeWaveformBuckets(in, 1, 1, 4, 1); // frames {100,0,-100}
        REQUIRE(b.size() == 1);
        CHECK(b[0].minVal == doctest::Approx(-100.0f / kNorm));
        CHECK(b[0].maxVal == doctest::Approx(100.0f / kNorm));
    }

    TEST_CASE("bucketing: multi-bucket proportional split")
    {
        const std::vector<short> in{10, -10, 20, -20};
        const auto b = resource::computeWaveformBuckets(in, 1, 0, 4, 2);
        REQUIRE(b.size() == 2);
        CHECK(b[0].minVal == doctest::Approx(-10.0f / kNorm)); // frames 0,1
        CHECK(b[0].maxVal == doctest::Approx(10.0f / kNorm));
        CHECK(b[1].minVal == doctest::Approx(-20.0f / kNorm)); // frames 2,3
        CHECK(b[1].maxVal == doctest::Approx(20.0f / kNorm));
    }

    TEST_CASE("bucketing: stereo merges channels into one min/max pair")
    {
        // 2 frames: {L=10,R=-40}, {L=20,R=-5}
        const std::vector<short> in{10, -40, 20, -5};
        const auto b = resource::computeWaveformBuckets(in, 2, 0, 2, 1);
        REQUIRE(b.size() == 1);
        CHECK(b[0].minVal == doctest::Approx(-40.0f / kNorm));
        CHECK(b[0].maxVal == doctest::Approx(20.0f / kNorm));
    }

    TEST_CASE("bucketing: bucketCount > frames yields stable count with empty buckets")
    {
        const std::vector<short> in{100, -100}; // 2 frames
        const auto b = resource::computeWaveformBuckets(in, 1, 0, 2, 4);
        REQUIRE(b.size() == 4);
        CHECK(b[0].minVal == 0.0f);
        CHECK(b[0].maxVal == 0.0f); // empty
        CHECK(b[1].maxVal == doctest::Approx(100.0f / kNorm));
        CHECK(b[2].minVal == 0.0f);
        CHECK(b[2].maxVal == 0.0f); // empty
        CHECK(b[3].minVal == doctest::Approx(-100.0f / kNorm));
    }

    TEST_CASE("bucketing: endFrame is clamped to available frames")
    {
        const std::vector<short> in{1, 2, 3, 4};
        const auto b = resource::computeWaveformBuckets(in, 1, 0, 100, 1); // clamp end -> 4
        REQUIRE(b.size() == 1);
        CHECK(b[0].maxVal == doctest::Approx(4.0f / kNorm));
    }

    TEST_CASE("bucketing: start>=end returns stable {0,0} buckets")
    {
        const std::vector<short> in{1, 2, 3, 4};
        const auto b = resource::computeWaveformBuckets(in, 1, 10, 3, 2); // clamps to empty range
        REQUIRE(b.size() == 2);
        CHECK(b[0].minVal == 0.0f);
        CHECK(b[0].maxVal == 0.0f);
        CHECK(b[1].minVal == 0.0f);
        CHECK(b[1].maxVal == 0.0f);
    }

    TEST_CASE("bucketing: degenerate inputs return empty")
    {
        CHECK(resource::computeWaveformBuckets(std::vector<short>{}, 1, 0, 0, 4).empty());
        CHECK(resource::computeWaveformBuckets(std::vector<short>{1, 2}, 0, 0, 2, 4).empty());
        CHECK(resource::computeWaveformBuckets(std::vector<short>{1, 2}, 1, 0, 2, 0).empty());
    }

    // ---------- computeWindowLevels ----------

    TEST_CASE("levels: silence is zero")
    {
        const std::vector<short> in{0, 0, 0, 0};
        const auto lv = resource::computeWindowLevels(in, 1, 0, 4);
        REQUIRE(lv.size() == 1);
        CHECK(lv[0].rms == doctest::Approx(0.0f));
        CHECK(lv[0].peak == doctest::Approx(0.0f));
    }

    TEST_CASE("levels: DC signal -> rms == |value|/32768")
    {
        const std::vector<short> in{16384, 16384, 16384, 16384}; // 0.5 full scale
        const auto lv = resource::computeWindowLevels(in, 1, 2, 4);
        REQUIRE(lv.size() == 1);
        CHECK(lv[0].rms == doctest::Approx(0.5f));
        CHECK(lv[0].peak == doctest::Approx(0.5f));
    }

    TEST_CASE("levels: full-scale square -> rms == 1")
    {
        const std::vector<short> in{-32768, -32768, -32768, -32768}; // -1.0 exactly
        const auto lv = resource::computeWindowLevels(in, 1, 2, 4);
        REQUIRE(lv.size() == 1);
        CHECK(lv[0].rms == doctest::Approx(1.0f));
        CHECK(lv[0].peak == doctest::Approx(1.0f));
    }

    TEST_CASE("levels: stereo channels metered independently")
    {
        // 2 frames: L=16384 (0.5), R=8192 (0.25)
        const std::vector<short> in{16384, 8192, 16384, 8192};
        const auto lv = resource::computeWindowLevels(in, 2, 1, 2);
        REQUIRE(lv.size() == 2);
        CHECK(lv[0].rms == doctest::Approx(0.5f));
        CHECK(lv[1].rms == doctest::Approx(0.25f));
    }

    TEST_CASE("levels: window clamped at start (no OOB), divides by actual count")
    {
        const std::vector<short> in{-32768, -32768, -32768, -32768};
        const auto lv = resource::computeWindowLevels(in, 1, 0, 8); // window runs off the start
        REQUIRE(lv.size() == 1);
        CHECK(lv[0].rms == doctest::Approx(1.0f)); // scans the 4 real frames only
    }

    TEST_CASE("levels: window entirely off the end returns zero (no OOB)")
    {
        const std::vector<short> in{-32768, -32768, -32768, -32768};
        const auto lv = resource::computeWindowLevels(in, 1, 1000, 4);
        REQUIRE(lv.size() == 1);
        CHECK(lv[0].rms == doctest::Approx(0.0f));
        CHECK(lv[0].peak == doctest::Approx(0.0f));
    }

    TEST_CASE("levels: channels == 0 returns empty")
    {
        CHECK(resource::computeWindowLevels(std::vector<short>{1, 2}, 0, 0, 2).empty());
    }

    // ---------- fftRadix2 ----------

    TEST_CASE("fft: unit impulse -> flat magnitude spectrum")
    {
        std::vector<std::complex<float>> a(8, {0.0f, 0.0f});
        a[0] = {1.0f, 0.0f};
        resource::fftRadix2(a);
        for (std::size_t k = 0; k < 8; ++k)
        {
            CHECK(std::abs(a[k]) == doctest::Approx(1.0f));
        }
    }

    TEST_CASE("fft: DC input -> energy only in bin 0")
    {
        std::vector<std::complex<float>> a(8, {1.0f, 0.0f});
        resource::fftRadix2(a);
        CHECK(std::abs(a[0]) == doctest::Approx(8.0f));
        for (std::size_t k = 1; k < 8; ++k)
        {
            CHECK(std::abs(a[k]) < 1e-3f);
        }
    }

    TEST_CASE("fft: non-power-of-two is a no-op")
    {
        std::vector<std::complex<float>> a(6, {2.0f, 0.0f});
        resource::fftRadix2(a);
        CHECK(a[0] == std::complex<float>(2.0f, 0.0f)); // unchanged
    }

    // ---------- computeSpectrum ----------

    TEST_CASE("spectrum: DC signal -> magnitude only in bin 0")
    {
        const std::vector<short> in(resource::kFftSize, 16384); // 0.5 full scale
        const auto mags = resource::computeSpectrum(in, 1, 0, WindowFn::Rect);
        REQUIRE(mags.size() == resource::kSpectrumBins);
        CHECK(mags[0] == doctest::Approx(0.5f));
        CHECK(mags[1] < 0.01f);
        CHECK(mags[100] < 0.01f);
    }

    TEST_CASE("spectrum: pure tone peaks at its exact bin")
    {
        const std::size_t bin = 64;
        std::vector<short> in(resource::kFftSize);
        for (std::size_t i = 0; i < resource::kFftSize; ++i)
        {
            const double phase = 2.0 * resource::kPi * static_cast<double>(bin) *
                                 static_cast<double>(i) / static_cast<double>(resource::kFftSize);
            in[i] = static_cast<short>(std::lround(32767.0 * std::cos(phase)));
        }
        const auto mags = resource::computeSpectrum(in, 1, 0, WindowFn::Rect);
        REQUIRE(mags.size() == resource::kSpectrumBins);

        const std::size_t argmax =
            static_cast<std::size_t>(std::max_element(mags.begin(), mags.end()) - mags.begin());
        CHECK(argmax == bin);
        CHECK(mags[bin] == doctest::Approx(1.0f).epsilon(0.02)); // ~full-scale, int16 rounding
        CHECK(mags[10] < 0.02f);                                 // exact-bin tone: no leakage
    }

    TEST_CASE("spectrum: stereo downmix keeps the tone's bin, halves an L-only amplitude")
    {
        const std::size_t bin = 64;
        std::vector<short> in(resource::kFftSize * 2, 0); // stereo, R silent
        for (std::size_t i = 0; i < resource::kFftSize; ++i)
        {
            const double phase = 2.0 * resource::kPi * static_cast<double>(bin) *
                                 static_cast<double>(i) / static_cast<double>(resource::kFftSize);
            in[i * 2] = static_cast<short>(std::lround(32767.0 * std::cos(phase))); // L only
        }
        const auto mags = resource::computeSpectrum(in, 2, 0, WindowFn::Rect);
        const std::size_t argmax =
            static_cast<std::size_t>(std::max_element(mags.begin(), mags.end()) - mags.begin());
        CHECK(argmax == bin);
        CHECK(mags[bin] == doctest::Approx(0.5f).epsilon(0.03)); // (L+0)/2 -> half amplitude
    }

    TEST_CASE("spectrum: degenerate inputs return zeroed bins")
    {
        CHECK(resource::computeSpectrum(std::vector<short>{}, 1, 0).size() == resource::kSpectrumBins);
        const auto z = resource::computeSpectrum(std::vector<short>{1, 2}, 0, 0);
        REQUIRE(z.size() == resource::kSpectrumBins);
        CHECK(z[0] == 0.0f);
    }

    // ---------- binSpectrumLog ----------

    TEST_CASE("logbins: flat spectrum -> every bar equals the level")
    {
        const std::vector<float> mags(resource::kSpectrumBins, 1.0f);
        const auto bars = resource::binSpectrumLog(mags, 44100, 32);
        REQUIRE(bars.size() == 32);
        for (float v : bars)
        {
            CHECK(v == doctest::Approx(1.0f));
        }
    }

    TEST_CASE("logbins: a single spike lands in exactly one bar as the max")
    {
        std::vector<float> mags(resource::kSpectrumBins, 0.0f);
        const std::size_t spikeBin = 46; // ~990 Hz at 44100 Hz / 2048-pt FFT
        mags[spikeBin] = 1.0f;
        const auto bars = resource::binSpectrumLog(mags, 44100, 32);
        REQUIRE(bars.size() == 32);

        const int nonZero = static_cast<int>(std::count_if(
            bars.begin(), bars.end(), [](float v) { return v > 0.0f; }));
        CHECK(nonZero == 1); // no spreading across bars
        const float maxBar = *std::max_element(bars.begin(), bars.end());
        CHECK(maxBar == doctest::Approx(1.0f));
    }

    TEST_CASE("logbins: degenerate inputs guarded")
    {
        CHECK(resource::binSpectrumLog(std::vector<float>{}, 44100, 8).size() == 8);
        CHECK(resource::binSpectrumLog(std::vector<float>(16, 1.0f), 0, 8).size() == 8);
        CHECK(resource::binSpectrumLog(std::vector<float>(16, 1.0f), 44100, 0).empty());
    }
}
