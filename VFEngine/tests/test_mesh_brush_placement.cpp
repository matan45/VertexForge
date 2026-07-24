#include <doctest.h>

#include "terrain/BrushFalloff.hpp"
#include "terrain/ValueNoise.hpp"
#include "meshbrush/MeshBrushMasks.hpp"

#include <random>
#include <cmath>
#include <glm/glm.hpp>

// ============================================================
// VK-1578 mesh brush placement parity: shared value noise, slope/height/noise
// masks (pure predicate), and seeded density-falloff thinning determinism.
// ============================================================

TEST_SUITE("MeshBrush") {

// ---- Shared value noise (terrain::valueNoise2D) ----

TEST_CASE("valueNoise2D: output stays within [0,1]") {
    for (float x = -5.0f; x <= 5.0f; x += 1.3f)
        for (float z = -5.0f; z <= 5.0f; z += 1.7f)
        {
            float v = terrain::valueNoise2D(x, z, 1337u);
            CHECK(v >= 0.0f);
            CHECK(v <= 1.0f);
        }
}

TEST_CASE("valueNoise2D: deterministic for identical (x,z,seed)") {
    CHECK(terrain::valueNoise2D(1.5f, 2.5f, 42u) == doctest::Approx(terrain::valueNoise2D(1.5f, 2.5f, 42u)));
    CHECK(terrain::valueNoise2D(-3.25f, 8.75f, 7u) == doctest::Approx(terrain::valueNoise2D(-3.25f, 8.75f, 7u)));
}

TEST_CASE("valueNoise2D: seed changes the field") {
    bool anyDiff = false;
    for (float x : {0.3f, 1.7f, 5.2f})
        for (float z : {0.1f, 2.9f})
            if (terrain::valueNoise2D(x, z, 1u) != terrain::valueNoise2D(x, z, 2u))
                anyDiff = true;
    CHECK(anyDiff);
}

// ---- Placement masks (meshbrush::passesMasks) ----

TEST_CASE("passesMasks: all masks off always accepts") {
    meshbrush::MeshBrushParams p; // every use*Mask defaults to false
    CHECK(meshbrush::passesMasks(p, -9999.0f, glm::vec3(0.0f, 0.0f, 0.0f), 12345.0f, -678.0f));
}

TEST_CASE("passesMasks: slope mask accepts inside the cosine band, rejects outside") {
    meshbrush::MeshBrushParams p;
    p.useSlopeMask = true;
    p.slopeMinCos = 0.5f; // accept normal.y in [0.5, 1.0]
    p.slopeMaxCos = 1.0f;

    CHECK(meshbrush::passesMasks(p, 0.0f, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.0f));   // flat -> accept
    CHECK(meshbrush::passesMasks(p, 0.0f, glm::vec3(0.0f, 0.5f, 0.0f), 0.0f, 0.0f));   // boundary -> accept
    CHECK_FALSE(meshbrush::passesMasks(p, 0.0f, glm::vec3(0.0f, 0.49f, 0.0f), 0.0f, 0.0f)); // steeper -> reject
}

TEST_CASE("passesMasks: height mask accepts inside the band, rejects outside") {
    meshbrush::MeshBrushParams p;
    p.useHeightMask = true;
    p.heightMin = 10.0f;
    p.heightMax = 20.0f;

    CHECK(meshbrush::passesMasks(p, 15.0f, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.0f)); // inside
    CHECK(meshbrush::passesMasks(p, 10.0f, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.0f)); // lower boundary
    CHECK(meshbrush::passesMasks(p, 20.0f, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.0f)); // upper boundary
    CHECK_FALSE(meshbrush::passesMasks(p, 9.9f, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.0f));
    CHECK_FALSE(meshbrush::passesMasks(p, 20.1f, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.0f));
}

TEST_CASE("passesMasks: noise mask rejects below threshold, accepts above") {
    meshbrush::MeshBrushParams p;
    p.useNoiseMask = true;
    p.noiseFrequency = 1.0f;
    p.noiseSeed = 7u;

    const float x = 3.3f, z = 7.1f;
    const float nv = terrain::valueNoise2D(x * p.noiseFrequency, z * p.noiseFrequency, p.noiseSeed);

    p.noiseThreshold = nv - 0.01f; // noise >= threshold -> accept
    CHECK(meshbrush::passesMasks(p, 0.0f, glm::vec3(0.0f, 1.0f, 0.0f), x, z));

    p.noiseThreshold = nv + 0.01f; // noise < threshold -> reject
    CHECK_FALSE(meshbrush::passesMasks(p, 0.0f, glm::vec3(0.0f, 1.0f, 0.0f), x, z));
}

// ---- Density falloff thinning (mirrors placeMeshes' reject loop) ----

namespace {
    // Replicate the brush's per-candidate accept test: keep when the uniform draw is
    // <= the falloff weight at the (area-uniform) normalized distance. Same RNG order as
    // MeshBrushServiceImpl::placeMeshes so this guards the thinning behavior + determinism.
    int keptUnderFalloff(terrain::BrushFalloff curve, uint32_t seed, int n)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> radiusDist(0.0f, 1.0f);
        std::uniform_real_distribution<float> unitDist(0.0f, 1.0f);
        int kept = 0;
        for (int i = 0; i < n; ++i)
        {
            float normDist = std::sqrt(radiusDist(rng)); // r/radius with r = radius*sqrt(u)
            float u = unitDist(rng);
            if (u <= terrain::applyFalloff(normDist, curve))
                ++kept;
        }
        return kept;
    }
}

TEST_CASE("falloff thinning: deterministic for a fixed seed") {
    CHECK(keptUnderFalloff(terrain::BrushFalloff::Smooth, 12345u, 2000)
          == keptUnderFalloff(terrain::BrushFalloff::Smooth, 12345u, 2000));
}

TEST_CASE("falloff thinning: Constant keeps everything, curves thin the edge") {
    const int n = 4000;
    int keptConstant = keptUnderFalloff(terrain::BrushFalloff::Constant, 999u, n);
    int keptLinear   = keptUnderFalloff(terrain::BrushFalloff::Linear,   999u, n);
    int keptSmooth   = keptUnderFalloff(terrain::BrushFalloff::Smooth,   999u, n);
    int keptSharp    = keptUnderFalloff(terrain::BrushFalloff::Sharp,    999u, n);

    CHECK(keptConstant == n);   // Constant never rejects (regression guard: falloff not ignored)
    CHECK(keptLinear < n);
    CHECK(keptSmooth < n);
    CHECK(keptSharp < n);
}

} // TEST_SUITE("MeshBrush")
