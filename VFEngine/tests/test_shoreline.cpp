#include <doctest.h>
#include <water/ShoreDepthField.hpp>
#include <water/ShoalingMath.hpp>
#include <water/ShoreWaveMath.hpp>

#include <cmath>
#include <vector>

// ============================================================
// VK-1605: shore depth field, shoaling and breaking shore waves
// (CPU twins of resources/shaders/water/water_shoaling.glsl)
// ============================================================

namespace
{
    // A beach sloping down toward +X: terrain height -0.1*x, so with the water at y = 0 the depth
    // is 0.1*x - negative (dry land) for x < 0, deepening out to sea.
    water::ShoreDepthField::HeightSampler slopeSampler(float slope = 0.1f)
    {
        return [slope](float worldX, float, float& outHeight)
        {
            outHeight = -slope * worldX;
            return true;
        };
    }

    void bakeFully(water::ShoreDepthField& field, const glm::vec2& center, float waterHeight,
                   water::ShoreDepthField::HeightSampler sampler)
    {
        field.beginRebake(center, waterHeight, std::move(sampler));
        while (!field.bakeRows(field.resolution()))
        {
            // beginRebake resets the cursor; one full-resolution tick always completes.
        }
    }
}

TEST_SUITE("ShoreDepthField") {

TEST_CASE("field starts bottomless so untouched scenes are unaffected") {
    water::ShoreDepthField field;

    // The whole "deep-ocean scenes are byte-identical" guarantee rests on this: before any bake
    // the field reports no bottom, and every shoaling factor built on it collapses to exactly 1.
    CHECK(field.sample(glm::vec2(0.0f)) == doctest::Approx(water::SHORE_FIELD_DEEP));
    CHECK(field.sample(glm::vec2(5000.0f, -3000.0f)) == doctest::Approx(water::SHORE_FIELD_DEEP));
    CHECK(field.version() == 0);
    CHECK_FALSE(field.hasBakedOnce());

    CHECK(water::greensLawGain(field.sample(glm::vec2(0.0f)), 120.0f) == 1.0f);
}

TEST_CASE("bake reproduces the analytic depth function") {
    water::ShoreDepthField field;
    field.configure(16, 160.0f);          // 10 m texels, cheap to verify exhaustively
    bakeFully(field, glm::vec2(0.0f), 0.0f, slopeSampler());

    CHECK(field.hasBakedOnce());
    CHECK(field.version() == 1);
    CHECK(field.center().x == doctest::Approx(0.0f));
    CHECK(field.origin().x == doctest::Approx(-80.0f));

    // Sampling exactly on a texel centre must return that texel, with no filtering error.
    const float texel = 160.0f / 16.0f;
    for (uint32_t i = 0; i < 16; ++i)
    {
        const float x = -80.0f + (static_cast<float>(i) + 0.5f) * texel;
        CHECK(field.sample(glm::vec2(x, 0.0f)) == doctest::Approx(0.1f * x).epsilon(1e-4));
    }

    // And between texels, because the function is linear and the filter is bilinear.
    CHECK(field.sample(glm::vec2(12.5f, 3.0f)) == doctest::Approx(1.25f).epsilon(1e-3));
}

TEST_CASE("water height shifts the whole field") {
    water::ShoreDepthField field;
    field.configure(16, 160.0f);
    bakeFully(field, glm::vec2(0.0f), 4.0f, slopeSampler());

    // depth = waterHeight - terrainHeight, so raising the ocean by 4 m deepens everything by 4 m.
    CHECK(field.sample(glm::vec2(20.0f, 0.0f)) == doctest::Approx(2.0f + 4.0f).epsilon(1e-3));
}

TEST_CASE("no terrain bakes to bottomless") {
    water::ShoreDepthField field;
    field.configure(8, 80.0f);
    bakeFully(field, glm::vec2(0.0f), 0.0f,
              [](float, float, float&) { return false; });

    CHECK(field.sample(glm::vec2(0.0f)) == doctest::Approx(water::SHORE_FIELD_DEEP));
    CHECK(field.sample(glm::vec2(-30.0f, 12.0f)) == doctest::Approx(water::SHORE_FIELD_DEEP));
}

TEST_CASE("sampling clamps to the window edge") {
    water::ShoreDepthField field;
    field.configure(16, 160.0f);
    bakeFully(field, glm::vec2(0.0f), 0.0f, slopeSampler());

    // Far outside the window the sampler repeats the border texel rather than extrapolating.
    const float atEdge = field.sample(glm::vec2(79.9f, 0.0f));
    CHECK(field.sample(glm::vec2(10000.0f, 0.0f)) == doctest::Approx(atEdge));

    const float atNegEdge = field.sample(glm::vec2(-79.9f, 0.0f));
    CHECK(field.sample(glm::vec2(-10000.0f, 0.0f)) == doctest::Approx(atNegEdge));
}

TEST_CASE("time-sliced bake equals a single-shot bake") {
    water::ShoreDepthField sliced;
    water::ShoreDepthField oneShot;
    sliced.configure(32, 320.0f);
    oneShot.configure(32, 320.0f);

    bakeFully(oneShot, glm::vec2(100.0f, -50.0f), 1.5f, slopeSampler(0.07f));

    sliced.beginRebake(glm::vec2(100.0f, -50.0f), 1.5f, slopeSampler(0.07f));
    uint32_t ticks = 0;
    while (!sliced.bakeRows(5))
    {
        ++ticks;
        REQUIRE(ticks < 1000);
    }

    REQUIRE(sliced.data().size() == oneShot.data().size());
    for (std::size_t i = 0; i < sliced.data().size(); ++i)
        CHECK(sliced.data()[i] == oneShot.data()[i]);   // bit-for-bit: same inputs, same order
}

TEST_CASE("an in-flight bake never leaks into what is being sampled") {
    water::ShoreDepthField field;
    field.configure(16, 160.0f);
    bakeFully(field, glm::vec2(0.0f), 0.0f, slopeSampler());

    const std::vector<float> committed = field.data();
    const uint32_t committedVersion = field.version();

    // A second bake somewhere completely different, deliberately left half-finished.
    field.beginRebake(glm::vec2(10000.0f, 0.0f), 0.0f, slopeSampler(0.5f));
    CHECK(field.bakeRows(4) == false);
    CHECK(field.isBaking());
    CHECK(field.bakeProgress() == doctest::Approx(4.0f / 16.0f));

    // Double buffering: the front buffer, its origin and its version are all untouched until the
    // last row lands. Without this a sample mid-bake would mix two different windows.
    CHECK(field.version() == committedVersion);
    CHECK(field.origin().x == doctest::Approx(-80.0f));
    for (std::size_t i = 0; i < committed.size(); ++i)
        CHECK(field.data()[i] == committed[i]);

    while (!field.bakeRows(4)) {}
    CHECK(field.version() == committedVersion + 1);
    CHECK(field.origin().x == doctest::Approx(10000.0f - 80.0f));
}

TEST_CASE("rebake triggers at a quarter of the window") {
    water::ShoreDepthField field;
    field.configure(16, 160.0f);

    CHECK(field.needsRebake(glm::vec2(0.0f)));   // nothing baked yet

    bakeFully(field, glm::vec2(0.0f), 0.0f, slopeSampler());
    CHECK_FALSE(field.needsRebake(glm::vec2(0.0f)));

    // A quarter (not a half) keeps a full quarter-window of real data ahead of the camera in
    // every direction; at a half the camera would sit on the border sampling clamped values.
    CHECK_FALSE(field.needsRebake(glm::vec2(39.9f, 0.0f)));
    CHECK(field.needsRebake(glm::vec2(40.1f, 0.0f)));
    CHECK(field.needsRebake(glm::vec2(0.0f, -40.1f)));

    // Chebyshev, not Euclidean: the window is a square, so what matters is the worst axis.
    CHECK_FALSE(field.needsRebake(glm::vec2(39.0f, 39.0f)));
}

TEST_CASE("gradient points offshore") {
    water::ShoreDepthField field;
    field.configure(64, 640.0f);
    bakeFully(field, glm::vec2(0.0f), 0.0f, slopeSampler());

    // depth = 0.1*x, so d(depth)/dx = 0.1 and there is no Z component.
    const glm::vec2 g = field.gradient(glm::vec2(0.0f, 0.0f), 10.0f);
    CHECK(g.x == doctest::Approx(0.1f).epsilon(1e-2));
    CHECK(g.y == doctest::Approx(0.0f).epsilon(1e-3));
}

} // TEST_SUITE ShoreDepthField

TEST_SUITE("TerrainHeightGrid") {

namespace
{
    // Two tiles side by side in X, 3x3 vertices each over a 2 m tile (1 m vertex spacing).
    water::TerrainHeightGrid makeGrid(std::vector<float>& heights, std::vector<uint8_t>& valid,
                                      bool secondTileValid)
    {
        constexpr uint32_t vpt = 3;
        heights.assign(2 * vpt * vpt, 0.0f);
        // Tile 0: height == worldX (0,1,2 along X, constant in Z)
        for (uint32_t z = 0; z < vpt; ++z)
            for (uint32_t x = 0; x < vpt; ++x)
                heights[z * vpt + x] = static_cast<float>(x);
        // Tile 1: continues the ramp (2,3,4)
        for (uint32_t z = 0; z < vpt; ++z)
            for (uint32_t x = 0; x < vpt; ++x)
                heights[vpt * vpt + z * vpt + x] = static_cast<float>(x) + 2.0f;

        valid = {1u, static_cast<uint8_t>(secondTileValid ? 1u : 0u)};

        water::TerrainHeightGrid grid;
        grid.worldOriginX = 0.0f;
        grid.worldOriginZ = 0.0f;
        grid.tileWorldSize = 2.0f;
        grid.vertexSpacing = 1.0f;
        grid.gridCountX = 2;
        grid.gridCountZ = 1;
        grid.verticesPerTile = vpt;
        grid.heights = heights.data();
        grid.heightCount = heights.size();
        grid.tileValid = valid.data();
        grid.tileValidCount = valid.size();
        return grid;
    }
}

TEST_CASE("packed tile lookup and bilinear interpolation") {
    std::vector<float> heights;
    std::vector<uint8_t> valid;
    const water::TerrainHeightGrid grid = makeGrid(heights, valid, true);

    float h = 0.0f;
    REQUIRE(grid.sample(0.0f, 0.0f, h));
    CHECK(h == doctest::Approx(0.0f));

    REQUIRE(grid.sample(1.5f, 0.5f, h));
    CHECK(h == doctest::Approx(1.5f));

    // Second tile: world 3.0 is local 1.0 in tile 1, whose ramp starts at 2.
    REQUIRE(grid.sample(3.0f, 0.0f, h));
    CHECK(h == doctest::Approx(3.0f));
}

TEST_CASE("outside the grid means no terrain, not height zero") {
    std::vector<float> heights;
    std::vector<uint8_t> valid;
    const water::TerrainHeightGrid grid = makeGrid(heights, valid, true);

    float h = 123.0f;
    CHECK_FALSE(grid.sample(-1.0f, 0.0f, h));
    CHECK_FALSE(grid.sample(50.0f, 0.0f, h));
    CHECK_FALSE(grid.sample(0.0f, 10.0f, h));
    CHECK(h == 123.0f);   // untouched on failure
}

TEST_CASE("an unstreamed tile reports no terrain") {
    std::vector<float> heights;
    std::vector<uint8_t> valid;
    const water::TerrainHeightGrid grid = makeGrid(heights, valid, false);

    float h = 0.0f;
    CHECK(grid.sample(1.0f, 0.5f, h));         // tile 0 is present
    // Tile 1's heights are still in the buffer, but the mask says it has no data. Without the
    // mask this would read as a genuine sea-level beach and kill the waves over the hole.
    CHECK_FALSE(grid.sample(3.0f, 0.5f, h));
}

TEST_CASE("an empty grid is simply no terrain") {
    water::TerrainHeightGrid grid;
    float h = 0.0f;
    CHECK_FALSE(grid.isValid());
    CHECK_FALSE(grid.sample(0.0f, 0.0f, h));
}

} // TEST_SUITE TerrainHeightGrid

TEST_SUITE("ShoalingMath") {

TEST_CASE("characteristic wavelength follows the Pierson-Moskowitz peak") {
    // lambda_p = 2*pi*U^2 / (0.877^2 * g). The default band winds are 12 / 8 / 4 m/s.
    CHECK(water::characteristicWavelength(12.0f) == doctest::Approx(119.9f).epsilon(0.01));
    CHECK(water::characteristicWavelength(8.0f) == doctest::Approx(53.3f).epsilon(0.01));
    CHECK(water::characteristicWavelength(4.0f) == doctest::Approx(13.3f).epsilon(0.01));
    CHECK(water::characteristicWavelength(0.0f) == doctest::Approx(0.0f));
}

TEST_CASE("gain is exactly 1 in deep water") {
    // Not "approximately 1" - literally 1.0f. This is what makes the deep side of the shore-field
    // window byte-identical to the pre-VK-1605 surface, with no seam where the effect starts.
    const float L = 120.0f;
    CHECK(water::greensLawGain(60.0f, L) == 1.0f);
    CHECK(water::greensLawGain(60.1f, L) == 1.0f);
    CHECK(water::greensLawGain(10000.0f, L) == 1.0f);
    CHECK(water::greensLawGain(water::SHORE_FIELD_DEEP, L) == 1.0f);
    CHECK(water::greensLawGain(5.0f, 0.0f) == 1.0f);   // degenerate wavelength
}

TEST_CASE("gain is continuous at the deep-water threshold") {
    const float L = 120.0f;
    // The normalisation by shoalingRawGain(PI) exists precisely so this does not step ~1%.
    CHECK(water::greensLawGain(59.99f, L) == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(water::greensLawGain(59.0f, L) == doctest::Approx(1.0f).epsilon(1e-3));
}

TEST_CASE("gain dips below 1 before it rises") {
    const float L = 120.0f;
    // Linear theory's shoaling coefficient bottoms out near kd = 1.2, i.e. d ~ 0.159 L. Waves
    // really do shrink slightly before they rear up - it is not an artefact.
    const float dip = water::greensLawGain(0.159f * L, L);
    CHECK(dip < 1.0f);
    CHECK(dip == doctest::Approx(0.921f).epsilon(0.02));

    // ...and then grows toward shore.
    CHECK(water::greensLawGain(0.02f * L, L) > 1.0f);
    CHECK(water::greensLawGain(0.01f * L, L) > water::greensLawGain(0.02f * L, L));
}

TEST_CASE("gain is bounded") {
    const float L = 120.0f;
    for (float d = 0.0f; d < 60.0f; d += 0.13f)
    {
        const float g = water::greensLawGain(d, L);
        CHECK(g >= 0.0f);
        CHECK(g <= water::SHOALING_MAX_GAIN);
        CHECK(std::isfinite(g));
    }
}

TEST_CASE("longer waves feel the bottom further out") {
    // The acceptance criterion: swell shoals well offshore while ripples are still in deep water.
    const float swell = water::characteristicWavelength(12.0f);    // ~120 m
    const float ripples = water::characteristicWavelength(4.0f);   // ~13 m

    CHECK(water::greensLawGain(30.0f, swell) != 1.0f);
    CHECK(water::greensLawGain(30.0f, ripples) == 1.0f);

    CHECK(water::greensLawGain(5.0f, ripples) != 1.0f);
}

TEST_CASE("shoalingScale is exactly 1 in deep water for any strength") {
    const float L = 120.0f;
    for (float strength : {0.0f, 0.25f, 0.5f, 1.0f})
    {
        CHECK(water::shoalingScale(200.0f, L, 1.2f, strength, 0.78f, 0.0f) == 1.0f);
        CHECK(water::shoalingScale(water::SHORE_FIELD_DEEP, L, 3.0f, strength, 0.78f, 0.0f) == 1.0f);
    }
    // Strength 0 is a hard no-op everywhere, not just offshore.
    CHECK(water::shoalingScale(0.5f, L, 1.2f, 0.0f, 0.78f, 0.0f) == 1.0f);
}

TEST_CASE("the breaking cap flattens waves at the waterline") {
    const float L = 40.0f;
    const float amplitude = 1.0f;

    // At zero depth no wave can stand at all.
    CHECK(water::shoalingScale(0.0f, L, amplitude, 1.0f, 0.78f, 0.0f) == doctest::Approx(0.0f));
    CHECK(water::shoalingScale(-2.0f, L, amplitude, 1.0f, 0.78f, 0.0f) == doctest::Approx(0.0f));

    // In 1 m of water the crest half-height is capped at 0.5 * 0.78 * 1 = 0.39, so a 1 m
    // amplitude band is scaled to 0.39.
    CHECK(water::shoalingScale(1.0f, L, amplitude, 1.0f, 0.78f, 0.0f) == doctest::Approx(0.39f));

    // minDepth moves the fully-flat line offshore.
    CHECK(water::shoalingScale(1.0f, L, amplitude, 1.0f, 0.78f, 1.0f) == doctest::Approx(0.0f));
}

TEST_CASE("strength blends linearly toward the full effect") {
    const float L = 40.0f;
    const float full = water::shoalingScale(1.0f, L, 1.0f, 1.0f, 0.78f, 0.0f);
    const float half = water::shoalingScale(1.0f, L, 1.0f, 0.5f, 0.78f, 0.0f);
    CHECK(half == doctest::Approx(1.0f + (full - 1.0f) * 0.5f));
}

TEST_CASE("chop compression runs from 0 on land to exactly 1 in deep water") {
    const float L = 40.0f;
    CHECK(water::chopCompression(0.0f, L, 1.0f) == doctest::Approx(0.0f));
    CHECK(water::chopCompression(20.0f, L, 1.0f) == 1.0f);     // exactly, at d = L/2
    CHECK(water::chopCompression(500.0f, L, 1.0f) == 1.0f);
    CHECK(water::chopCompression(3.0f, L, 0.0f) == 1.0f);      // strength 0 is a no-op

    float previous = -1.0f;
    for (float d = 0.0f; d <= 20.0f; d += 0.5f)
    {
        const float c = water::chopCompression(d, L, 1.0f);
        CHECK(c >= previous);
        CHECK(c >= 0.0f);
        CHECK(c <= 1.0f);
        previous = c;
    }
}

TEST_CASE("window fade is 1 inside, 0 at the border, and monotone between") {
    const glm::vec2 origin(0.0f, 0.0f);
    const float window = 100.0f;
    const float fadeStart = 0.88f;

    CHECK(water::shoreWindowFade(glm::vec2(50.0f, 50.0f), origin, window, fadeStart) == 1.0f);
    // The border and everything beyond it contributes nothing - this is what makes re-centring
    // the window invisible to both the surface and to buoyancy.
    CHECK(water::shoreWindowFade(glm::vec2(0.0f, 50.0f), origin, window, fadeStart) == 0.0f);
    CHECK(water::shoreWindowFade(glm::vec2(100.0f, 50.0f), origin, window, fadeStart) == 0.0f);
    CHECK(water::shoreWindowFade(glm::vec2(-500.0f, 50.0f), origin, window, fadeStart) == 0.0f);

    float previous = 2.0f;
    for (float x = 50.0f; x <= 100.0f; x += 1.0f)
    {
        const float f = water::shoreWindowFade(glm::vec2(x, 50.0f), origin, window, fadeStart);
        CHECK(f <= previous + 1e-6f);
        CHECK(f >= 0.0f);
        CHECK(f <= 1.0f);
        previous = f;
    }
}

TEST_CASE("fading the strength reaches exactly 1 at the window border") {
    // How the shader and getOceanHeightAt actually combine the two: strength *= fade. At the
    // border fade is 0, strength is 0, and shoalingScale returns literal 1.0f - the continuity
    // guarantee that stops a buoyant body popping when the field re-centres.
    const glm::vec2 origin(0.0f, 0.0f);
    const float window = 100.0f;
    const float fade = water::shoreWindowFade(glm::vec2(0.0f, 50.0f), origin, window, 0.88f);
    CHECK(water::shoalingScale(0.4f, 40.0f, 1.0f, 1.0f * fade, 0.78f, 0.0f) == 1.0f);
    CHECK(water::chopCompression(0.4f, 40.0f, 1.0f * fade) == 1.0f);
}

} // TEST_SUITE ShoalingMath

TEST_SUITE("ShoreWaveMath") {

namespace
{
    water::ShoreWaveParams surfParams()
    {
        water::ShoreWaveParams p;
        p.amplitude = 0.5f;
        p.length = 12.0f;
        p.speed = 0.35f;
        p.breakDepth = 3.0f;
        p.breakRange = 1.0f;
        p.crestFoam = 0.6f;
        p.crestFoamThreshold = 0.55f;
        return p;
    }
}

TEST_CASE("envelope is exactly zero on land and in deep water") {
    const auto p = surfParams();

    CHECK(water::shoreWaveEnvelope(0.0f, p) == 0.0f);
    CHECK(water::shoreWaveEnvelope(-5.0f, p) == 0.0f);
    // breakDepth + breakRange = 4 m; beyond that there is no surf, exactly zero (not a tail).
    CHECK(water::shoreWaveEnvelope(4.0f, p) == 0.0f);
    CHECK(water::shoreWaveEnvelope(50.0f, p) == 0.0f);
    CHECK(water::shoreWaveEnvelope(water::SHORE_FIELD_DEEP, p) == 0.0f);
}

TEST_CASE("envelope peaks inside the surf zone") {
    const auto p = surfParams();
    // Plateau between breakRange (1 m) and breakDepth (3 m).
    CHECK(water::shoreWaveEnvelope(2.0f, p) == doctest::Approx(1.0f));
    CHECK(water::shoreWaveEnvelope(0.5f, p) > 0.0f);
    CHECK(water::shoreWaveEnvelope(0.5f, p) < 1.0f);
    CHECK(water::shoreWaveEnvelope(3.5f, p) > 0.0f);
    CHECK(water::shoreWaveEnvelope(3.5f, p) < 1.0f);
}

TEST_CASE("a breakDepth inside the fade width does not collapse the envelope") {
    auto p = surfParams();
    p.breakDepth = 0.2f;    // smaller than breakRange
    p.breakRange = 1.0f;
    // Clamped internally, so there is still a live surf zone rather than a silently dead feature.
    CHECK(water::shoreWaveEnvelope(1.0f, p) > 0.0f);
}

TEST_CASE("amplitude zero disables the deformer entirely") {
    auto p = surfParams();
    p.amplitude = 0.0f;

    CHECK(water::shoreWaveEnvelope(2.0f, p) == 0.0f);
    CHECK(water::shoreWaveHeight(2.0f, 3.7f, p) == 0.0f);
    CHECK(water::shoreCrestFoam(2.0f, 3.7f, p) == 0.0f);
}

TEST_CASE("profile is a non-negative bore, never a symmetric sine") {
    for (float phase = -20.0f; phase <= 20.0f; phase += 0.05f)
    {
        const float v = water::shoreWaveProfile(phase);
        CHECK(v >= 0.0f);
        CHECK(v <= 1.0f);
    }
    // Crest-biased: the midpoint of the cycle sits well below half height.
    CHECK(water::shoreWaveProfile(0.0f) == doctest::Approx(0.125f));
    CHECK(water::shoreWaveProfile(water::SHOALING_PI * 0.5f) == doctest::Approx(1.0f));
}

TEST_CASE("crests travel shoreward") {
    const auto p = surfParams();

    // Follow one crest: holding the phase fixed, depth = L*(c - t*speed), so as time advances the
    // crest is found in SHALLOWER water. (With the ticket's original minus sign it would run out
    // to sea instead.)
    const float d1 = 3.0f;
    const float t1 = 10.0f;
    const float dt = 0.5f;
    const float d2 = d1 - p.length * p.speed * dt;

    CHECK(d2 < d1);
    CHECK(water::shoreWavePhase(d2, t1 + dt, p) == doctest::Approx(water::shoreWavePhase(d1, t1, p)));
}

TEST_CASE("phase is periodic in depth") {
    const auto p = surfParams();
    const float phaseA = water::shoreWavePhase(2.0f, 0.0f, p);
    const float phaseB = water::shoreWavePhase(2.0f + p.length, 0.0f, p);
    CHECK(phaseB - phaseA == doctest::Approx(water::SHOALING_TWO_PI));
}

TEST_CASE("height is bounded by the amplitude and never negative") {
    const auto p = surfParams();
    for (float d = -1.0f; d <= 8.0f; d += 0.05f)
    {
        for (float t = 0.0f; t < 4.0f; t += 0.17f)
        {
            const float h = water::shoreWaveHeight(d, t, p);
            CHECK(h >= 0.0f);
            CHECK(h <= p.amplitude + 1e-6f);
        }
    }
}

TEST_CASE("crest foam stays in range and only appears in the surf zone") {
    const auto p = surfParams();

    CHECK(water::shoreCrestFoam(-1.0f, 1.0f, p) == 0.0f);
    CHECK(water::shoreCrestFoam(20.0f, 1.0f, p) == 0.0f);

    float peak = 0.0f;
    for (float t = 0.0f; t < 6.0f; t += 0.01f)
    {
        const float f = water::shoreCrestFoam(2.0f, t, p);
        CHECK(f >= 0.0f);
        CHECK(f <= p.crestFoam + 1e-6f);
        peak = std::max(peak, f);
    }
    CHECK(peak > 0.0f);   // the crest does actually reach the foam threshold
}

} // TEST_SUITE ShoreWaveMath
