#include <doctest.h>

#include <terrain/TerrainRVTBudget.hpp>
#include <material/TerrainBlendCurve.hpp>
#include <render/virtualtexture/VTTypes.hpp>
#include <render/gpudriven/terrain/TerrainRVTLayout.hpp>

#include <cmath>

// ============================================================
// VK-1620 - RVT mesh-into-terrain blending: the world-height plane.
//
// External meshes (rocks, cliffs) sample the terrain's runtime virtual texture and blend its
// surface onto their base, so props melt into the ground instead of meeting it at a hard line.
// That needs a 5th RVT plane carrying the terrain's world height.
//
// What is pinned here is everything about the feature that is PURE, because the rest needs a GPU:
// the pool geometry the plane costs, the height encode/decode contract the bake and the blend sit
// on either side of, the blend curve the shader mirrors, and the quad interpolation that has to
// match the terrain's triangulation rather than being plain bilinear.
// ============================================================

TEST_CASE("VK-1620 the world-height plane's page cost, at the shipped budget")
{
    // These four numbers ARE the reason the plane is a setting rather than always-on. The pool is
    // sized by AREA, so +2 bytes/texel costs far more page count than it costs bytes.
    const auto legacy = terrain::terrainRVTPoolGeometry(128, false, false);
    CHECK(legacy.planeCount == 2);
    CHECK(legacy.bytesPerTexel == 8);
    CHECK(legacy.capacityPages == 1024);

    const auto legacyH = terrain::terrainRVTPoolGeometry(128, false, true);
    CHECK(legacyH.planeCount == 3);
    CHECK(legacyH.bytesPerTexel == 10);
    CHECK(legacyH.poolDim == 3584);
    CHECK(legacyH.capacityPages == 784); // a 23% cut for +2 B/texel

    const auto detail = terrain::terrainRVTPoolGeometry(128, true, false);
    CHECK(detail.planeCount == 4);
    CHECK(detail.capacityPages == 400);

    const auto detailH = terrain::terrainRVTPoolGeometry(128, true, true);
    CHECK(detailH.planeCount == 5);
    CHECK(detailH.bytesPerTexel == 22);
    CHECK(detailH.poolDim == 2432);
    CHECK(detailH.capacityPages == 361);

    // The height plane is a SECOND, INDEPENDENT axis - it must add exactly one plane and exactly
    // two bytes on top of whichever base layout is live, not replace anything.
    for (const bool detailMaps : {false, true})
    {
        CHECK(terrain::terrainRVTPlaneCount(detailMaps, true)
              == terrain::terrainRVTPlaneCount(detailMaps, false) + 1);
        CHECK(terrain::terrainRVTBytesPerTexel(detailMaps, true)
              == terrain::terrainRVTBytesPerTexel(detailMaps, false)
                 + terrain::TERRAIN_RVT_WORLD_HEIGHT_BYTES_PER_TEXEL);
    }
}

TEST_CASE("VK-1620 the world-height plane is appended LAST and is R16_UNORM")
{
    // Plane indices are addresses: mesh_terrain.glsl binds set-5 samplers by index and
    // terrain_rvt_bake.glsl writes MRT locations by index. Inserting the height plane anywhere but
    // the end would silently re-point both. The bake shader's #ifdef ladder picks location 2 or 4
    // from TERRAIN_DETAIL_MAPS; terrainRVTWorldHeightPlaneIndex is the C++ side of that same choice
    // and the two must agree or height is written into the emission plane.
    for (const bool detailMaps : {false, true})
    {
        const auto layout = render::gpudriven::terrainRVTLayout(detailMaps, true);
        const uint32_t heightIndex = render::gpudriven::terrainRVTWorldHeightPlaneIndex(detailMaps);

        REQUIRE(layout.planeFormats.size() == heightIndex + 1u);
        CHECK(layout.planeFormats[heightIndex] == render::gpudriven::TERRAIN_RVT_WORLD_HEIGHT_FORMAT);
        CHECK(render::gpudriven::TERRAIN_RVT_WORLD_HEIGHT_FORMAT == vk::Format::eR16Unorm);

        // The base planes must be untouched by the new axis.
        const auto base = render::gpudriven::terrainRVTLayout(detailMaps, false);
        for (size_t i = 0; i < base.planeFormats.size(); ++i)
            CHECK(layout.planeFormats[i] == base.planeFormats[i]);
    }
}

TEST_CASE("VK-1620 height encode/decode survives R16_UNORM quantization")
{
    // The bake encodes with terrainRVTEncodeHeight and the scene-mesh blend decodes with
    // terrainRVTDecodeHeight, through 16 bits of unorm. If that round trip drifts, props sit at the
    // wrong height above the ground - the one error this feature cannot tolerate, because the whole
    // effect is a comparison against the surface.
    const float lo = -10.0f, hi = 100.0f; // the shipped TerrainTileConfig defaults
    const float quantum = (hi - lo) / 65535.0f;

    for (const float y : {-10.0f, -9.5f, 0.0f, 12.25f, 55.0f, 99.9f, 100.0f})
    {
        const float t = terrain::terrainRVTEncodeHeight(y, lo, hi);
        CHECK(t >= 0.0f);
        CHECK(t <= 1.0f);
        // Simulate what the R16_UNORM plane physically stores.
        const float stored = std::round(t * 65535.0f) / 65535.0f;
        const float back = terrain::terrainRVTDecodeHeight(stored, lo, hi);
        CHECK(std::fabs(back - y) <= quantum);
    }

    // 1.68 mm on the default range: comfortably finer than any blend band, and fine enough that the
    // terrain normal reconstructed from this plane's gradient does not terrace visibly.
    CHECK(quantum < 0.002f);

    // Out of range CLAMPS rather than wrapping. A prop dropped far below the terrain floor must
    // read "terrain is at the bottom of its range", never "terrain is above me".
    CHECK(terrain::terrainRVTEncodeHeight(-500.0f, lo, hi) == 0.0f);
    CHECK(terrain::terrainRVTEncodeHeight(9000.0f, lo, hi) == 1.0f);

    // A degenerate authored range must not divide by zero.
    CHECK(terrain::terrainRVTEncodeHeight(5.0f, 10.0f, 10.0f) == 0.0f);
}

TEST_CASE("VK-1620 quad interpolation follows the terrain's ANTI-DIAGONAL triangulation")
{
    // This is the finding that killed the obvious implementation. TerrainTileGenerator splits every
    // quad (topLeft, bottomLeft, topRight) + (topRight, bottomLeft, bottomRight), so the hinge runs
    // from (x+1,z) to (x,z+1). Plain bilinear disagrees with the rendered surface exactly along
    // that hinge, and the disagreement is largest on a ridge - which is where a floating prop is
    // most obvious.
    const float h00 = 0.0f, h11 = 0.0f;   // valley corners
    const float h10 = 10.0f, h01 = 10.0f; // ridge corners, along the hinge

    // Dead centre of the quad sits ON the hinge, where the two disagree maximally.
    const float surface = terrain::terrainQuadHeight(h00, h10, h01, h11, 0.5f, 0.5f);
    const float bilinear = (h00 + h10 + h01 + h11) * 0.25f;
    CHECK(surface == doctest::Approx(10.0f)); // the hinge IS the ridge line
    CHECK(bilinear == doctest::Approx(5.0f)); // bilinear cuts the ridge in half
    CHECK(std::fabs(surface - bilinear) > 4.0f);

    // Every corner must be reproduced exactly, in both triangles.
    CHECK(terrain::terrainQuadHeight(h00, h10, h01, h11, 0.0f, 0.0f) == doctest::Approx(h00));
    CHECK(terrain::terrainQuadHeight(h00, h10, h01, h11, 1.0f, 0.0f) == doctest::Approx(h10));
    CHECK(terrain::terrainQuadHeight(h00, h10, h01, h11, 0.0f, 1.0f) == doctest::Approx(h01));
    CHECK(terrain::terrainQuadHeight(h00, h10, h01, h11, 1.0f, 1.0f) == doctest::Approx(h11));

    // Continuity across the hinge: approaching it from either triangle must agree, or the bake
    // would write a seam down every quad diagonal.
    for (const float fx : {0.1f, 0.25f, 0.5f, 0.75f, 0.9f})
    {
        const float lower = terrain::terrainQuadHeight(h00, h10, h01, h11, fx, 1.0f - fx - 1e-5f);
        const float upper = terrain::terrainQuadHeight(h00, h10, h01, h11, fx, 1.0f - fx + 1e-5f);
        CHECK(lower == doctest::Approx(upper).epsilon(0.001));
    }

    // A planar quad is the degenerate case where the two agree - proof the split only matters when
    // the quad is non-planar, i.e. that this is not gratuitously different from bilinear.
    const float p00 = 1.0f, p10 = 2.0f, p01 = 3.0f, p11 = 4.0f; // h = 1 + fx + 2*fz
    for (const float fx : {0.0f, 0.3f, 0.5f, 0.8f, 1.0f})
        for (const float fz : {0.0f, 0.3f, 0.5f, 0.8f, 1.0f})
        {
            const float tri = terrain::terrainQuadHeight(p00, p10, p01, p11, fx, fz);
            CHECK(tri == doctest::Approx(1.0f + fx + 2.0f * fz));
        }
}

TEST_CASE("VK-1620 the blend curve is what the shader mirrors")
{
    using namespace material;

    const float band = 0.5f;
    const float contrast = 2.0f;

    // At or below the surface the prop is fully terrain; clear of the band it is fully itself.
    // Both fall out of the clamped smoothstep rather than being special-cased, which is what keeps
    // the GLSL copy short enough to stay honest.
    CHECK(terrainBlendAlpha(-1.0f, band, contrast) == doctest::Approx(1.0f));
    CHECK(terrainBlendAlpha(0.0f, band, contrast) == doctest::Approx(1.0f));
    CHECK(terrainBlendAlpha(band, band, contrast) == doctest::Approx(0.0f));
    CHECK(terrainBlendAlpha(band * 2.0f, band, contrast) == doctest::Approx(0.0f));

    // Monotonically decreasing across the band - a non-monotonic curve would read as a band of
    // terrain colour floating above the contact line.
    float previous = 2.0f;
    for (int i = 0; i <= 20; ++i)
    {
        const float a = terrainBlendAlpha(band * (static_cast<float>(i) / 20.0f), band, contrast);
        CHECK(a <= previous + 1e-6f);
        CHECK(a >= 0.0f);
        CHECK(a <= 1.0f);
        previous = a;
    }

    // Higher contrast pulls the blend DOWN toward the ground, so at any given height it must be
    // weaker, never stronger.
    CHECK(terrainBlendAlpha(band * 0.5f, band, 4.0f) < terrainBlendAlpha(band * 0.5f, band, 1.0f));

    // The contrast floor is 1, not 0 - and that is load-bearing. pow(x, 0) is 1 everywhere, which
    // would blend the WHOLE band at full strength and then cut hard at its top edge: the exact
    // artefact the feature exists to remove. Clamping INTO the range (never to 0) is the same
    // convention the terrain material's per-layer opt-in scalars use.
    CHECK(MIN_TERRAIN_BLEND_CONTRAST == 1.0f);
    CHECK(clampTerrainBlendContrast(0.0f) == MIN_TERRAIN_BLEND_CONTRAST);
    CHECK(clampTerrainBlendContrast(-5.0f) == MIN_TERRAIN_BLEND_CONTRAST);
    CHECK(clampTerrainBlendContrast(1000.0f) == MAX_TERRAIN_BLEND_CONTRAST);
    CHECK(terrainBlendAlpha(band * 0.5f, band, 0.0f) < 1.0f); // clamped, so still a real falloff

    // A zero band would make the blend a step function at the contact line.
    CHECK(MIN_TERRAIN_BLEND_BAND > 0.0f);
    CHECK(clampTerrainBlendBand(0.0f) == MIN_TERRAIN_BLEND_BAND);
    CHECK(clampTerrainBlendBand(-1.0f) == MIN_TERRAIN_BLEND_BAND);
    CHECK(clampTerrainBlendBand(1e9f) == MAX_TERRAIN_BLEND_BAND);

    // Defaults must be inside their own ranges, or a fresh material starts clamped.
    CHECK(clampTerrainBlendBand(DEFAULT_TERRAIN_BLEND_BAND) == DEFAULT_TERRAIN_BLEND_BAND);
    CHECK(clampTerrainBlendContrast(DEFAULT_TERRAIN_BLEND_CONTRAST) == DEFAULT_TERRAIN_BLEND_CONTRAST);
}

TEST_CASE("VK-1620 band and contrast survive the half-float packing into instanceData.z")
{
    using namespace material;

    // The two scalars ride PerDrawData.instanceData.z as two halfs so PerDrawData stays exactly
    // 256 bytes - growing it would cost 6.25% of a buffer sized for 700k draws. Half precision is
    // ~3 decimal digits, which is far more than a blend band authored in centimetres needs, but the
    // round trip has to actually work or props blend at the wrong depth.
    for (const float band : {MIN_TERRAIN_BLEND_BAND, 0.05f, 0.35f, 1.0f, 7.5f, MAX_TERRAIN_BLEND_BAND})
        for (const float contrast : {MIN_TERRAIN_BLEND_CONTRAST, 1.5f, 2.0f, 4.0f, MAX_TERRAIN_BLEND_CONTRAST})
        {
            const uint32_t packed = packTerrainBlendParams(band, contrast);
            float outBand = 0.0f, outContrast = 0.0f;
            unpackTerrainBlendParams(packed, outBand, outContrast);
            CHECK(outBand == doctest::Approx(band).epsilon(0.001));
            CHECK(outContrast == doctest::Approx(contrast).epsilon(0.001));
        }

    // Packing CLAMPS, because the shader trusts these values and does not re-clamp them.
    float band = 0.0f, contrast = 0.0f;
    unpackTerrainBlendParams(packTerrainBlendParams(-5.0f, 0.0f), band, contrast);
    CHECK(band == doctest::Approx(MIN_TERRAIN_BLEND_BAND).epsilon(0.001));
    CHECK(contrast == doctest::Approx(MIN_TERRAIN_BLEND_CONTRAST).epsilon(0.001));

    unpackTerrainBlendParams(packTerrainBlendParams(1e9f, 1e9f), band, contrast);
    CHECK(band == doctest::Approx(MAX_TERRAIN_BLEND_BAND).epsilon(0.001));
    CHECK(contrast == doctest::Approx(MAX_TERRAIN_BLEND_CONTRAST).epsilon(0.001));

    // Band is the LOW half and contrast the HIGH half, matching GLSL unpackHalf2x16's .x/.y. Get
    // this backwards and a 0.35 m band becomes a 0.35 exponent - which the shader would then use
    // unclamped.
    const uint32_t packed = packTerrainBlendParams(1.0f, 2.0f);
    CHECK(halfBitsToFloat(static_cast<uint16_t>(packed & 0xFFFFu)) == doctest::Approx(1.0f));
    CHECK(halfBitsToFloat(static_cast<uint16_t>(packed >> 16)) == doctest::Approx(2.0f));
}
