#include <doctest.h>

#include <terrain/TerrainMaterialTypes.hpp>
#include <terrain/TerrainParallax.hpp>
#include "render/gpudriven/terrain/TerrainLayerPBRResolver.hpp"
#include "render/gpudriven/terrain/TerrainParallaxParams.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

// ============================================================
// VK-1625 - terrain parallax from composited height (POM-lite).
//
// The march runs on the GPU, so nothing here can be executed against a device. What these cases pin
// is everything that decides whether the feature is correct or silently ruinous:
//
//   * the OFF path is an EXACT identity, not an approximate one - a height field constant at 1
//     (which is every ORM packed before VK-1609, alpha force-written to 255) must produce a bitwise
//     vec2(0) offset, or enabling parallax would visibly shift legacy terrain;
//   * the reference plane. Classic POM sinks a flat height field of value c to depth (1-c)*D, which
//     is a view-rotating constant offset - the surface appears to swim. That is REACHABLE today
//     because OrmTexturePacker::DEFAULT_HEIGHT is 128, so these cases pin both the artefact and the
//     one knob that removes it, and pin that the knob's default is the exact identity;
//   * the world-offset -> UV mapping is EXACT rather than approximate, which is the entire reason
//     this implementation needs no tangent basis and works unchanged on triplanar cave fragments;
//   * every scalar that reaches the GPU is clamped at one seam (resolveTerrainParallax), and two of
//     those clamps are correctness invariants rather than taste;
//   * the permutation flag is derived from the CLAMPED value, so a number an artist typed out of
//     range cannot switch a shader permutation on.
//
// CPU-only: no Vulkan device, no window.
// ============================================================

using namespace terrain;

namespace
{
    // A height field that ignores position: the degenerate case every identity argument rests on.
    auto constantField(float h)
    {
        return [h](float) { return h; };
    }

    // Solve with the settings a fragment would actually carry, so the cases exercise the same
    // entry point the shader mirrors.
    float solveDepth(float depth, uint32_t steps, const std::function<float(float)>& field)
    {
        return parallaxSolve(depth, steps, field).depth;
    }
}

TEST_CASE("terrain parallax is an exact identity on unauthored height")
{
    // Every ORM packed before VK-1609 has alpha force-written to 255 (see the fix comment in
    // OrmTexturePacker.cpp), so h == 1 everywhere. The ray must meet the surface at depth 0.
    SUBCASE("a field constant at 1 solves to bitwise zero depth")
    {
        for (uint32_t steps : {1u, 2u, 4u, 8u, 16u, 32u})
        {
            for (float depth : {0.001f, 0.03f, 0.25f, PARALLAX_MAX_DEPTH})
            {
                const float z = solveDepth(depth, steps, constantField(1.0f));
                // Bitwise, not approximate: this is what makes "legacy content is unaffected" a
                // guarantee rather than a hope.
                CHECK(z == 0.0f);
            }
        }
    }

    SUBCASE("a zero-depth offset leaves the base UV bitwise unchanged")
    {
        // The shader does `baseUV += uvStep * z`. At z == 0 the product is a signed zero, and adding
        // a signed zero is the identity for every float - including for a base UV that is itself 0.
        const glm::vec2 uvStep{-0.37f, 0.91f};
        for (glm::vec2 baseUV : {glm::vec2{0.0f, 0.0f}, glm::vec2{-0.0f, 12.5f}, glm::vec2{1e-30f, -4.25f}})
        {
            const glm::vec2 shifted = baseUV + uvStep * 0.0f;
            CHECK(shifted.x == baseUV.x);
            CHECK(shifted.y == baseUV.y);
        }
    }

    SUBCASE("a layer with no ORM contributes the top plane at any reference height")
    {
        // The shader hard-codes 1.0 for a layer without an ORM. Because the reciprocal is always >= 1,
        // remapping that value would clamp back to 1 anyway - so the two spellings agree, and a
        // material can never be dragged into displacing on a layer that has no height at all.
        for (float ref : {PARALLAX_MIN_REFERENCE_HEIGHT, 0.25f, 0.5f, 1.0f})
        {
            CHECK(parallaxNormalizeHeight(1.0f, parallaxInvReferenceHeight(ref)) == 1.0f);
        }
    }
}

TEST_CASE("terrain parallax reference plane")
{
    SUBCASE("the default reference height is the exact identity")
    {
        // The knob must be free for every material that never touches it.
        const float invRef = parallaxInvReferenceHeight(PARALLAX_DEFAULT_REFERENCE_HEIGHT);
        CHECK(invRef == 1.0f);
        for (int i = 0; i <= 255; ++i)
        {
            const float h = static_cast<float>(i) / 255.0f;
            CHECK(parallaxNormalizeHeight(h, invRef) == h);
        }
    }

    SUBCASE("a flat field below the top plane sinks - the swim artefact, pinned")
    {
        // This is the behaviour, not a bug: with the reference at the top, a field that never
        // reaches it sits uniformly (1-c)*depth deep, and that depth turns into a UV offset that
        // rotates with the camera. Pinned so the artist-facing consequence is machine-documented.
        const float depth = 0.04f;
        for (float c : {0.25f, 0.5f, 0.75f})
        {
            const float z = solveDepth(depth, 16u, constantField(c));
            CHECK(z == doctest::Approx(depth * (1.0f - c)).epsilon(1e-5));
            CHECK(z > 0.0f); // i.e. it really does displace
        }
    }

    SUBCASE("setting the reference to the field's own value removes it")
    {
        // Exactly representable ratios, so the remap lands on 1.0 to the bit and the solve is
        // bitwise zero - the same guarantee as the legacy-ORM case above.
        const float depth = 0.04f;
        for (float c : {0.5f, 0.25f, 0.125f})
        {
            const float invRef = parallaxInvReferenceHeight(c);
            const float remapped = parallaxNormalizeHeight(c, invRef);
            CHECK(remapped == 1.0f);
            CHECK(solveDepth(depth, 16u, constantField(remapped)) == 0.0f);
        }
    }

    SUBCASE("the alpha-128 no-height ORM is neutralised to within a rounding step")
    {
        // OrmTexturePacker::DEFAULT_HEIGHT is 128, so this is the realistic case rather than a
        // contrived one. 128/255 has no exact reciprocal in binary32, so the claim here is "the
        // residual displacement is negligible", not "bitwise zero" - stated honestly.
        const float c = 128.0f / 255.0f;
        const float depth = 0.04f;
        const float remapped = parallaxNormalizeHeight(c, parallaxInvReferenceHeight(c));
        CHECK(remapped == doctest::Approx(1.0f).epsilon(1e-6));

        const float unfixed = solveDepth(depth, 16u, constantField(c));
        const float fixed = solveDepth(depth, 16u, constantField(remapped));
        CHECK(unfixed > 0.4f * depth);          // the artefact is real and large
        CHECK(fixed < 1e-6f * depth);           // and the knob removes it to well below a pixel
    }
}

TEST_CASE("terrain parallax march finds the intersection")
{
    SUBCASE("a linear field is solved EXACTLY, at any step count")
    {
        // h rises with depth: h(z) = 0.5 + 0.5*z/D, so the surface depth s(z) = D*(1-h) = 0.5D - 0.5z
        // falls as the ray descends. They meet where z == 0.5D - 0.5z, i.e. at z = D/3.
        //
        // Both the ray depth and the surface depth are linear in z, so f(z) = s(z) - z is linear and
        // the secant refinement is EXACT rather than approximate. That is the property worth pinning:
        // the step count buys bracketing, and the refinement recovers the crossing regardless of how
        // coarse that bracket was.
        const float depth = 0.06f;
        auto field = [depth](float z) { return std::clamp(0.5f + 0.5f * z / depth, 0.0f, 1.0f); };

        for (uint32_t steps : {1u, 2u, 3u, 4u, 8u, 16u, 32u})
        {
            const auto solved = parallaxSolve(depth, steps, field);
            CHECK(solved.crossed);
            CHECK(solved.depth == doctest::Approx(depth / 3.0f).epsilon(1e-5));
        }
    }

    SUBCASE("a ray steeper than the surface meets it at the entry point")
    {
        // The flat top plane of a pit: h == 1 until zEdge, so the surface depth is 0 there and the
        // very first sample is already at or below it. The refinement then pins the crossing at 0,
        // which is the correct answer — the ray never got under anything.
        const float depth = 0.08f;
        const float zEdge = 0.03f;
        auto field = [zEdge](float z) { return (z < zEdge) ? 1.0f : 0.0f; };

        for (uint32_t steps : {4u, 8u, 16u, 32u})
        {
            REQUIRE(depth / static_cast<float>(steps) < zEdge); // the first sample lands on the top
            const auto solved = parallaxSolve(depth, steps, field);
            CHECK(solved.crossed);
            CHECK(solved.depth == 0.0f);
        }
    }

    SUBCASE("the deepest sample always crosses, so the no-hit fallback is unreachable")
    {
        // Worth stating because it justifies the shape of the code. The surface depth is D*(1-h) with
        // h in [0,1], so s(D) <= D for every field: the last sample is always at or below the surface
        // and `found` is always 1. The mix(depth, zStar, found) fallback is therefore defensive only,
        // and no field can steer shading through an unrefined answer.
        const float depth = 0.05f;
        // A surface that stays below the ray for the whole volume until the very last sample:
        // h(z) = clamp(0.5 - 2z/D) gives s(z) = 0.5D + 2z > z everywhere except at the floor.
        auto sinking = [depth](float z) { return std::clamp(0.5f - 2.0f * z / depth, 0.0f, 1.0f); };
        const auto solved = parallaxSolve(depth, 8u, sinking);
        CHECK(solved.crossed);
        CHECK(solved.depth == doctest::Approx(depth));

        // And the same for a field pinned at the bottom of the volume.
        const auto floored = parallaxSolve(depth, 8u, constantField(0.0f));
        CHECK(floored.crossed);
        CHECK(floored.depth == doctest::Approx(depth));
    }

    SUBCASE("the solved depth never leaves the displacement volume")
    {
        const float depth = 0.06f;
        // A field with structure, so the search actually has work to do.
        auto field = [](float z) { return 0.5f + 0.5f * std::sin(400.0f * z); };
        for (uint32_t steps : {1u, 3u, 8u, 32u})
        {
            const float z = solveDepth(depth, steps, field);
            CHECK(z >= 0.0f);
            CHECK(z <= depth);
        }
    }

    SUBCASE("a step count outside the authored range is clamped, not honoured")
    {
        // The GPU takes the count from a UBO; the CPU mirror re-clamps so a test cannot accidentally
        // assert behaviour the shader would never see.
        const float z0 = solveDepth(0.04f, 0u, constantField(0.5f));
        const float z1 = solveDepth(0.04f, PARALLAX_MIN_STEPS, constantField(0.5f));
        CHECK(z0 == z1);
    }
}

TEST_CASE("terrain parallax fades")
{
    SUBCASE("distance fade is 1 near, 0 past the end, and monotone between")
    {
        const float start = 15.0f;
        const float end = 25.0f;
        CHECK(parallaxDistanceFade(0.0f, start, end) == 1.0f);
        CHECK(parallaxDistanceFade(start, start, end) == 1.0f);
        CHECK(parallaxDistanceFade(end, start, end) == 0.0f);
        CHECK(parallaxDistanceFade(1000.0f, start, end) == 0.0f);

        float prev = 1.1f;
        for (int i = 0; i <= 40; ++i)
        {
            const float d = start + (end - start) * (static_cast<float>(i) / 40.0f);
            const float f = parallaxDistanceFade(d, start, end);
            CHECK(f <= prev);
            prev = f;
        }
    }

    SUBCASE("past the fade the effective depth is zero, so nothing is marched at all")
    {
        // Zero depth is what closes the shader's `if (tpDepth > 0.0)` guard, which is the only reason
        // distant terrain pays nothing for this feature.
        const float d = parallaxEffectiveDepth(0.05f, 40.0f, 15.0f, 25.0f, 1.0f);
        CHECK(d == 0.0f);
    }

    SUBCASE("grazing views fade out before the reciprocal clamp engages")
    {
        // THE INVARIANT: the clamp may only engage where the fade has already reached exactly zero.
        // If the floor sat above the fade's lower edge there would be a band of angles that are both
        // clamped and still visible, and the clamp's own iso-line would draw the crease the fade
        // exists to prevent. (Also static_assert-ed in the header, so this cannot regress silently.)
        CHECK(PARALLAX_MIN_NDOTV <= PARALLAX_GRAZE_FADE_MIN);
        CHECK(parallaxGrazeFade(PARALLAX_MIN_NDOTV) == 0.0f);
        CHECK(parallaxGrazeFade(0.0f) == 0.0f);
        CHECK(parallaxGrazeFade(PARALLAX_GRAZE_FADE_MIN) == 0.0f);
        CHECK(parallaxGrazeFade(1.0f) == 1.0f);
        // Backfacing fragments must not produce an offset either.
        CHECK(parallaxGrazeFade(-0.5f) == 0.0f);
    }

    SUBCASE("the ray step stays bounded at every angle")
    {
        const glm::vec3 n{0.0f, 1.0f, 0.0f};
        for (int i = 0; i <= 180; ++i)
        {
            const float a = static_cast<float>(i) * 3.14159265f / 180.0f;
            const glm::vec3 v{std::sin(a), std::cos(a), 0.0f};
            const glm::vec3 step = parallaxRayStep(v, n);
            CHECK(std::isfinite(step.x));
            CHECK(std::isfinite(step.y));
            CHECK(std::isfinite(step.z));
            // |step| == |V| / max(dot, floor) <= 1 / floor.
            CHECK(glm::length(step) <= 1.0f / PARALLAX_MIN_NDOTV + 1e-4f);
        }
    }
}

TEST_CASE("terrain parallax world offset maps into UV space exactly")
{
    // This is the property that removes the tangent basis. mesh_terrain.glsl builds its base UV as an
    // affine function of world position, so the UV at P+d is the UV at P plus a term that depends
    // only on d - which is why one hoisted uvStep is enough for the whole march, and why cave
    // fragments need no special case.
    const float scale = 0.1f;
    const std::vector<glm::vec3> normals = {
        {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.6f, 0.8f, 0.0f},
        {0.0f, 0.28f, 0.96f}, {0.577f, 0.577f, 0.577f}, {1.0f, 0.0f, 0.0f},
    };
    const std::vector<glm::vec3> positions = {
        {0.0f, 0.0f, 0.0f}, {123.5f, -7.25f, -998.75f}, {-4096.0f, 512.0f, 4096.0f},
    };
    const std::vector<glm::vec3> deltas = {
        {0.0f, 0.0f, 0.0f}, {0.01f, -0.02f, 0.03f}, {-0.5f, 0.25f, 0.125f},
    };

    // The UV construction being mirrored, both arms.
    auto buildUV = [scale](const glm::vec3& p, const glm::vec3& bw, bool pureXZ)
    {
        if (pureXZ)
            return glm::vec2(p.x, p.z) * scale;
        return (glm::vec2(p.x, p.z) * bw.y + glm::vec2(p.x, p.y) * bw.z + glm::vec2(p.y, p.z) * bw.x) * scale;
    };

    for (bool pureXZ : {false, true})
    {
        for (const auto& n : normals)
        {
            const glm::vec3 bw = parallaxTriplanarWeights(n);
            // The weights are a partition of unity, which is what keeps the blended UV a true
            // convex combination of the three projections.
            CHECK(bw.x + bw.y + bw.z == doctest::Approx(1.0f).epsilon(1e-5));

            for (const auto& p : positions)
            {
                for (const auto& d : deltas)
                {
                    const glm::vec2 uvP = buildUV(p, bw, pureXZ);
                    const glm::vec2 uvPd = buildUV(p + d, bw, pureXZ);
                    const glm::vec2 direct = uvPd - uvP;
                    const glm::vec2 mapped = parallaxProjectWorldOffset(d, bw, scale, pureXZ);

                    // The tolerance is the CANCELLATION bound on `direct` — the reference value, whose
                    // two terms are large and nearly equal far from the origin — not slack for the
                    // mapping, which is exact by construction. Deriving it rather than picking a
                    // constant is what keeps this test meaningful for the small deltas parallax
                    // actually produces (centimetres) at world positions of several kilometres.
                    const float eps = std::numeric_limits<float>::epsilon();
                    const float tolX = 8.0f * eps * (std::abs(uvP.x) + std::abs(uvPd.x)) + 1e-7f;
                    const float tolY = 8.0f * eps * (std::abs(uvP.y) + std::abs(uvPd.y)) + 1e-7f;
                    CHECK(std::abs(mapped.x - direct.x) <= tolX);
                    CHECK(std::abs(mapped.y - direct.y) <= tolY);
                }
            }
        }
    }

    SUBCASE("the offset is linear in depth, so one hoisted step covers the whole march")
    {
        const glm::vec3 bw = parallaxTriplanarWeights({0.3f, 0.9f, 0.31f});
        const glm::vec3 rayStep{-0.4f, -1.2f, 0.7f};
        const glm::vec2 unit = parallaxProjectWorldOffset(rayStep, bw, scale, false);
        for (float z : {0.0f, 0.003f, 0.02f, 0.5f})
        {
            const glm::vec2 atZ = parallaxProjectWorldOffset(rayStep * z, bw, scale, false);
            CHECK(atZ.x == doctest::Approx(unit.x * z).epsilon(1e-5).scale(1e-6));
            CHECK(atZ.y == doctest::Approx(unit.y * z).epsilon(1e-5).scale(1e-6));
        }
    }
}

TEST_CASE("terrain parallax resolver is the single clamp seam")
{
    using render::gpudriven::resolveTerrainParallax;
    using render::gpudriven::terrainMaterialWantsParallax;

    SUBCASE("an unauthored material resolves to off")
    {
        const TerrainParallaxSettings defaults{};
        const auto params = resolveTerrainParallax(defaults);
        CHECK(params.depthMetres == 0.0f);
        CHECK_FALSE(terrainMaterialWantsParallax(params));
        // The default must also be the exact-identity remap, or merely having the feature compiled
        // in would perturb a material that never asked for it.
        CHECK(params.invReferenceHeight == 1.0f);
    }

    SUBCASE("depth is clamped, and the permutation follows the CLAMPED value")
    {
        TerrainParallaxSettings s{};
        s.depthMetres = -5.0f;
        CHECK(resolveTerrainParallax(s).depthMetres == 0.0f);
        CHECK_FALSE(terrainMaterialWantsParallax(resolveTerrainParallax(s)));

        s.depthMetres = 1000.0f;
        CHECK(resolveTerrainParallax(s).depthMetres == PARALLAX_MAX_DEPTH);
        CHECK(terrainMaterialWantsParallax(resolveTerrainParallax(s)));
    }

    SUBCASE("fadeEnd is forced strictly past fadeStart - smoothstep with equal edges is undefined")
    {
        TerrainParallaxSettings s{};
        s.depthMetres = 0.03f;
        s.fadeStart = 30.0f;
        s.fadeEnd = 30.0f; // exactly equal: the degenerate case
        auto p = resolveTerrainParallax(s);
        CHECK(p.fadeEnd > p.fadeStart);

        s.fadeEnd = 1.0f; // inverted
        p = resolveTerrainParallax(s);
        CHECK(p.fadeEnd > p.fadeStart);
    }

    SUBCASE("referenceHeight is clamped away from zero BEFORE being inverted")
    {
        TerrainParallaxSettings s{};
        s.depthMetres = 0.03f;
        for (float ref : {0.0f, -1.0f, 1e-30f})
        {
            auto t = s;
            t.referenceHeight = ref;
            const auto p = resolveTerrainParallax(t);
            // A zero here would upload an infinity and turn the shader's clamp into a NaN factory.
            CHECK(std::isfinite(p.invReferenceHeight));
            CHECK(p.invReferenceHeight == doctest::Approx(1.0f / PARALLAX_MIN_REFERENCE_HEIGHT));
        }
        // And it is never below 1, so a no-ORM layer's hard-coded 1.0 can never be pushed off the
        // top plane by the remap.
        for (float ref : {0.01f, 0.5f, 1.0f, 2.0f, 1e9f})
        {
            auto t = s;
            t.referenceHeight = ref;
            CHECK(resolveTerrainParallax(t).invReferenceHeight >= 1.0f);
        }
    }

    SUBCASE("steps are clamped into the authored range")
    {
        TerrainParallaxSettings s{};
        s.depthMetres = 0.03f;
        s.steps = 0u;
        CHECK(resolveTerrainParallax(s).steps == PARALLAX_MIN_STEPS);
        s.steps = 100000u;
        CHECK(resolveTerrainParallax(s).steps == PARALLAX_MAX_STEPS);
    }

    SUBCASE("the UBO mirror keeps the size the shader declares")
    {
        // The GLSL side is a std140 block of eight 4-byte scalars. If this ever drifts, the shader
        // reads the wrong fields with no validation error at all.
        CHECK(sizeof(render::gpudriven::TerrainParallaxUBOData) == 32);
    }
}
