#include <doctest.h>
#include <water/HexTiling.hpp>

#include <cmath>
#include <vector>

// ============================================================
// VK-1604: hex tile-and-blend anti-tiling (CPU twin of hex_tiling.glsl)
// ============================================================

namespace
{
    constexpr float kCellScale = 1.0f;
    constexpr float kContrast = 4.0f;

    // Deterministic pseudo-random field standing in for an FFT displacement map. Zero-mean and
    // roughly Gaussian, like the real thing (a sum of many independent Fourier modes with no DC
    // term), so the variance-preservation assertions below are meaningful.
    float sampleField(const glm::vec2& uv)
    {
        float total = 0.0f;
        for (int k = 1; k <= 6; ++k)
        {
            const float f = static_cast<float>(k);
            total += std::sin(6.28318f * f * uv.x + 1.3f * f)
                   * std::cos(6.28318f * f * uv.y + 0.7f * f);
        }
        return total / std::sqrt(6.0f);
    }

    float variance(const std::vector<float>& values)
    {
        if (values.size() < 2)
            return 0.0f;
        float mean = 0.0f;
        for (float v : values)
            mean += v;
        mean /= static_cast<float>(values.size());

        float sumSq = 0.0f;
        for (float v : values)
            sumSq += (v - mean) * (v - mean);
        return sumSq / static_cast<float>(values.size() - 1);
    }
}

TEST_SUITE("HexTiling") {

// ---- hash ----

TEST_CASE("hexHashUint: deterministic and well spread") {
    CHECK(water::hexHashUint(0u) == water::hexHashUint(0u));
    CHECK(water::hexHashUint(12345u) == water::hexHashUint(12345u));
    CHECK(water::hexHashUint(0u) != water::hexHashUint(1u));

    // Neighbouring inputs must not produce neighbouring outputs, or adjacent hex cells would
    // pick near-identical offsets and the tiling would still be visible.
    for (uint32_t i = 0; i < 64; ++i)
    {
        const uint32_t a = water::hexHashUint(i);
        const uint32_t b = water::hexHashUint(i + 1);
        const uint32_t diff = (a > b) ? (a - b) : (b - a);
        CHECK(diff > 1000u);
    }
}

TEST_CASE("hexCellOffset: in [0,1), deterministic, distinct per cell") {
    for (int y = -4; y <= 4; ++y)
    {
        for (int x = -4; x <= 4; ++x)
        {
            const glm::vec2 offset = water::hexCellOffset(x, y);
            CHECK(offset.x >= 0.0f);
            CHECK(offset.x < 1.0f);
            CHECK(offset.y >= 0.0f);
            CHECK(offset.y < 1.0f);

            const glm::vec2 again = water::hexCellOffset(x, y);
            CHECK(offset.x == doctest::Approx(again.x));
            CHECK(offset.y == doctest::Approx(again.y));
        }
    }

    auto differs = [](const glm::vec2& a, const glm::vec2& b) {
        return a.x != b.x || a.y != b.y;
    };
    CHECK(differs(water::hexCellOffset(0, 0), water::hexCellOffset(1, 0)));
    CHECK(differs(water::hexCellOffset(0, 0), water::hexCellOffset(0, 1)));
    // Negative cell ids must not alias onto positive ones (the lattice spans both).
    CHECK(differs(water::hexCellOffset(-1, 0), water::hexCellOffset(1, 0)));
}

// ---- weights ----

TEST_CASE("hexComputeBlend: weights are non-negative and sum to 1") {
    for (int iy = 0; iy < 40; ++iy)
    {
        for (int ix = 0; ix < 40; ++ix)
        {
            const glm::vec2 uv(static_cast<float>(ix) * 0.137f,
                               static_cast<float>(iy) * 0.211f);
            const water::HexBlend hb = water::hexComputeBlend(uv, kCellScale, kContrast);

            float sum = 0.0f;
            for (uint32_t i = 0; i < 3; ++i)
            {
                CHECK(hb.weight[i] >= 0.0f);
                CHECK(hb.weight[i] <= 1.0f);
                sum += hb.weight[i];
            }
            CHECK(sum == doctest::Approx(1.0f));
        }
    }
}

TEST_CASE("hexComputeBlend: remains well-formed at large world coordinates") {
    // Ocean UVs are world-metres / patchSize and grow without bound as the camera travels.
    // This is the test that would catch a regression from the integer hash back to a
    // fract(sin(...)) hash, which degenerates for large arguments.
    for (float base : {0.0f, 1.0e3f, 1.0e4f, 1.0e5f})
    {
        const glm::vec2 uv(base + 0.317f, base + 0.613f);
        const water::HexBlend hb = water::hexComputeBlend(uv, kCellScale, kContrast);

        float sum = 0.0f;
        for (uint32_t i = 0; i < 3; ++i)
        {
            CHECK(std::isfinite(hb.weight[i]));
            CHECK(hb.weight[i] >= 0.0f);
            sum += hb.weight[i];
        }
        CHECK(sum == doctest::Approx(1.0f));
        CHECK(std::isfinite(hb.varianceScale));
        CHECK(hb.varianceScale >= 1.0f);
    }
}

TEST_CASE("hexComputeBlend: varianceScale is 1 at a vertex and sqrt(3) at a triangle centre") {
    // At a lattice vertex one weight dominates -> no variance loss to restore.
    // Contrast 1 keeps the raw barycentric weights so the analytic values hold.
    const water::HexBlend atVertex = water::hexComputeBlend(glm::vec2(0.0f, 0.0f), kCellScale, 1.0f);
    CHECK(atVertex.varianceScale == doctest::Approx(1.0f).epsilon(0.02));

    // Search for the most balanced point on the lattice; there the three weights approach 1/3
    // each and the scale approaches sqrt(3).
    float bestSpread = 1.0f;
    float bestScale = 0.0f;
    for (int iy = 0; iy < 120; ++iy)
    {
        for (int ix = 0; ix < 120; ++ix)
        {
            const glm::vec2 uv(static_cast<float>(ix) / 120.0f, static_cast<float>(iy) / 120.0f);
            const water::HexBlend hb = water::hexComputeBlend(uv, kCellScale, 1.0f);
            const float spread = std::abs(hb.weight[0] - 1.0f / 3.0f)
                               + std::abs(hb.weight[1] - 1.0f / 3.0f)
                               + std::abs(hb.weight[2] - 1.0f / 3.0f);
            if (spread < bestSpread)
            {
                bestSpread = spread;
                bestScale = hb.varianceScale;
            }
        }
    }
    REQUIRE(bestSpread < 0.05f);
    CHECK(bestScale == doctest::Approx(std::sqrt(3.0f)).epsilon(0.05));
}

TEST_CASE("hexComputeBlend: is continuous across cell boundaries") {
    // A discontinuity in the blend shows up as a visible seam grid on the water. Step across a
    // dense sweep and require the blended value never to jump.
    const float step = 1.0f / 512.0f;
    float previous = 0.0f;
    bool first = true;

    for (int i = 0; i < 2048; ++i)
    {
        const glm::vec2 uv(static_cast<float>(i) * step, 0.37f);
        const water::HexBlend hb = water::hexComputeBlend(uv, kCellScale, kContrast);
        const float blended = water::hexCombineVariancePreserving(hb,
            sampleField(hb.uv[0]), sampleField(hb.uv[1]), sampleField(hb.uv[2]));

        if (!first)
            CHECK(std::abs(blended - previous) < 0.35f);

        previous = blended;
        first = false;
    }
}

// ---- blending ----

TEST_CASE("hexCombineVariancePreserving: preserves variance where a plain mean loses it") {
    std::vector<float> exemplar;
    std::vector<float> blended;
    std::vector<float> naiveMean;

    for (int iy = 0; iy < 64; ++iy)
    {
        for (int ix = 0; ix < 64; ++ix)
        {
            const glm::vec2 uv(static_cast<float>(ix) * 0.0173f + 0.011f,
                               static_cast<float>(iy) * 0.0231f + 0.007f);

            exemplar.push_back(sampleField(uv));

            const water::HexBlend hb = water::hexComputeBlend(uv, kCellScale, kContrast);
            const float s0 = sampleField(hb.uv[0]);
            const float s1 = sampleField(hb.uv[1]);
            const float s2 = sampleField(hb.uv[2]);

            blended.push_back(water::hexCombineVariancePreserving(hb, s0, s1, s2));
            naiveMean.push_back(water::hexCombineMeanPreserving(hb, s0, s1, s2));
        }
    }

    const float exemplarVar = variance(exemplar);
    const float blendedVar = variance(blended);
    const float naiveVar = variance(naiveMean);

    REQUIRE(exemplarVar > 0.0f);

    // The point of the whole exercise: a plain weighted mean measurably flattens the waves.
    // Asserting only "blended ~= exemplar" would pass even if the blend did nothing, so the
    // second half of this test is what gives the first half meaning.
    CHECK(naiveVar < exemplarVar * 0.9f);
    CHECK(blendedVar > naiveVar);
    CHECK(blendedVar == doctest::Approx(exemplarVar).epsilon(0.35));
}

TEST_CASE("hexCombineMeanPreserving: never produces negative foam") {
    // Foam is non-negative and decidedly non-Gaussian. Running it through the
    // variance-preserving combine would scale deviations around a non-zero mean and can push
    // the result below zero, punching holes in the foam after the clamp in water.glsl.
    for (int iy = 0; iy < 48; ++iy)
    {
        for (int ix = 0; ix < 48; ++ix)
        {
            const glm::vec2 uv(static_cast<float>(ix) * 0.041f, static_cast<float>(iy) * 0.037f);
            const water::HexBlend hb = water::hexComputeBlend(uv, kCellScale, kContrast);

            // Non-negative "foam" field in [0,1].
            auto foamAt = [](const glm::vec2& p) {
                return 0.5f + 0.5f * std::sin(11.0f * p.x) * std::cos(7.0f * p.y);
            };

            const float blended = water::hexCombineMeanPreserving(hb,
                foamAt(hb.uv[0]), foamAt(hb.uv[1]), foamAt(hb.uv[2]));

            CHECK(blended >= 0.0f);
            CHECK(blended <= 1.0f);
        }
    }
}

TEST_CASE("hexCombineMeanPreserving: a constant field blends to that constant") {
    for (int i = 0; i < 32; ++i)
    {
        const glm::vec2 uv(static_cast<float>(i) * 0.089f, static_cast<float>(i) * 0.053f);
        const water::HexBlend hb = water::hexComputeBlend(uv, kCellScale, kContrast);
        CHECK(water::hexCombineMeanPreserving(hb, 0.75f, 0.75f, 0.75f) == doctest::Approx(0.75f));
    }
}

// ---- band mask ----

TEST_CASE("hexBandEnabled: reads the per-band mask bits") {
    CHECK_FALSE(water::hexBandEnabled(0x6u, 0u));   // default: swell untiled
    CHECK(water::hexBandEnabled(0x6u, 1u));
    CHECK(water::hexBandEnabled(0x6u, 2u));

    CHECK_FALSE(water::hexBandEnabled(0x0u, 0u));
    CHECK(water::hexBandEnabled(0x7u, 0u));
    CHECK(water::hexBandEnabled(0x1u, 0u));
    CHECK_FALSE(water::hexBandEnabled(0x1u, 1u));
}

} // TEST_SUITE("HexTiling")
