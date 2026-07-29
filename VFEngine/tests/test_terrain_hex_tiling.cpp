#include <doctest.h>

#include "test_repo_scan_helpers.hpp"

#include <graph/TerrainCompositeSnippet.hpp>
#include <terrain/TerrainHexTiling.hpp>
#include <terrain/TerrainMaterialTypes.hpp>
#include "render/gpudriven/GPUDrivenTypes.hpp"
#include "render/gpudriven/terrain/TerrainLayerPBRResolver.hpp"
#include "render/material/MaterialPBRExtractor.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

// ============================================================
// VK-1612 - terrain hex-tile stochastic sampling.
//
// Mikkelsen, "Practical Real-Time Hex-Tiling", JCGT 11(3); weight sharpening per Burley,
// "On Histogram-Preserving Blending for Randomized Texture Tiling", JCGT 8(4), Eq. 5.
//
// The sampling itself is GPU-only, so these cases pin the two properties that decide whether it
// looks right, plus the gating that decides whether it costs anything:
//
//   * THE LATTICE IS EQUILATERAL. The engine already had a hex implementation (VK-1604's ocean),
//     and its skew is the TRANSPOSE of the correct one, giving triangle sides 1.118 / 1.065 /
//     0.866 instead of 1 / 1 / 1. Barycentric weights are affine-invariant, so the ocean's own
//     tests cannot see it, and on zero-mean FFT displacement it is invisible. On albedo it is a
//     directional bias - exactly the artifact this story exists to remove. That is why terrain got
//     its own header, and this is the case the water suite is missing.
//   * THE ALBEDO BLEND IS CONVEX. The ocean's variance-preserving combine multiplies colour by up
//     to sqrt(3) at a triangle centre; the RVT albedo plane is R8G8B8A8_SRGB, so that overshoot
//     would be clamped and permanently BAKED into the page. Heitz & Neyret say of that operator
//     that even with a correct mean it "overshoots (it creates too dark and too bright pixels)"
//     and "does not prevent wrong colors or ghosting from appearing". A convex combination cannot.
//
// CPU-only: no Vulkan device, no window.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    using editor::graph::buildTerrainCompositeLoop;
    using render::gpudriven::ResolvedTerrainLayerPBR;
    using render::gpudriven::resolveTerrainLayerPBR;
    using render::gpudriven::terrainMaterialWantsHexTiling;

    fs::path repoRoot()
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");
        return *root;
    }

    size_t countOccurrences(const std::string& haystack, const std::string& needle)
    {
        if (needle.empty()) return 0;
        size_t count = 0;
        for (size_t pos = haystack.find(needle); pos != std::string::npos;
             pos = haystack.find(needle, pos + needle.size()))
        {
            ++count;
        }
        return count;
    }

    // Drops `// ...` comments so a guard can assert on what the shader DOES rather than on what
    // its header comment happens to mention.
    std::string stripLineComments(const std::string& src)
    {
        std::string out;
        out.reserve(src.size());
        size_t i = 0;
        while (i < src.size())
        {
            if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '/')
            {
                while (i < src.size() && src[i] != '\n') ++i;
            }
            else
            {
                out.push_back(src[i++]);
            }
        }
        return out;
    }

    float cellDistance(int ax, int ay, int bx, int by)
    {
        float axf, ayf, bxf, byf;
        terrain::hexTerrainCellCentre(ax, ay, axf, ayf);
        terrain::hexTerrainCellCentre(bx, by, bxf, byf);
        return std::hypot(axf - bxf, ayf - byf);
    }
}

TEST_SUITE("TerrainHexTiling")
{
    // ------------------------------------------------------------------
    // 1. The lattice.
    // ------------------------------------------------------------------

    TEST_CASE("the lattice is equilateral")
    {
        // The check the ocean twin does not have. Its skew produces 1.118 / 1.065 / 0.866; ours
        // must produce 1 / 1 / 1, or the hex cells are elongated along one axis and the blend
        // reintroduces a directional pattern of its own.
        for (int cy = -3; cy <= 3; ++cy)
        {
            for (int cx = -3; cx <= 3; ++cx)
            {
                CAPTURE(cx);
                CAPTURE(cy);
                const float a = cellDistance(cx, cy, cx + 1, cy);
                const float b = cellDistance(cx, cy, cx, cy + 1);
                const float c = cellDistance(cx + 1, cy, cx, cy + 1);
                CHECK(a == doctest::Approx(1.0f).epsilon(1e-5));
                CHECK(b == doctest::Approx(1.0f).epsilon(1e-5));
                CHECK(c == doctest::Approx(1.0f).epsilon(1e-5));
            }
        }
    }

    TEST_CASE("the three taps are always distinct neighbouring cells")
    {
        // Two taps landing on the same cell would collapse a 3-way blend into a 2-way one and
        // undo the de-repetition for that fragment.
        for (int i = 0; i < 60; ++i)
        {
            for (int j = 0; j < 60; ++j)
            {
                const float u = static_cast<float>(i) * 0.081f - 2.0f;
                const float v = static_cast<float>(j) * 0.073f - 2.0f;
                const auto hb = terrain::hexTerrainComputeBlend(u, v, 1.0f, 4.0f, 0.0f);
                CAPTURE(u);
                CAPTURE(v);
                const bool distinct01 = hb.tap[0].cellX != hb.tap[1].cellX || hb.tap[0].cellY != hb.tap[1].cellY;
                const bool distinct02 = hb.tap[0].cellX != hb.tap[2].cellX || hb.tap[0].cellY != hb.tap[2].cellY;
                const bool distinct12 = hb.tap[1].cellX != hb.tap[2].cellX || hb.tap[1].cellY != hb.tap[2].cellY;
                CHECK(distinct01);
                CHECK(distinct02);
                CHECK(distinct12);
            }
        }
    }

    // ------------------------------------------------------------------
    // 2. The weights.
    // ------------------------------------------------------------------

    TEST_CASE("weights are non-negative and sum to one everywhere")
    {
        // A partition of unity is what makes the blend convex, which is what makes it impossible
        // to clip against the sRGB8 bake target.
        for (float gamma : {1.0f, 4.0f, 7.0f, 16.0f})
        {
            for (int i = 0; i < 40; ++i)
            {
                for (int j = 0; j < 40; ++j)
                {
                    const float u = static_cast<float>(i) * 0.11f - 1.5f;
                    const float v = static_cast<float>(j) * 0.13f - 1.5f;
                    const auto hb = terrain::hexTerrainComputeBlend(u, v, 1.0f, gamma, 0.0f);
                    CAPTURE(gamma);
                    CAPTURE(u);
                    CAPTURE(v);
                    float sum = 0.0f;
                    for (const auto& t : hb.tap)
                    {
                        CHECK(t.weight >= 0.0f);
                        CHECK(std::isfinite(t.weight));
                        sum += t.weight;
                    }
                    CHECK(sum == doctest::Approx(1.0f).epsilon(1e-4));
                }
            }
        }
    }

    TEST_CASE("a higher contrast sharpens toward a single winner")
    {
        // Burley Eq. 5 is the entire answer to the wash-out a plain 3-tap mean would produce. His
        // tuning: ghosting below gamma 2, visible hexagonal structure above 8, 4 is the sweet spot.
        // Pick a point that is genuinely off-centre so there IS a winner to sharpen toward.
        const float u = 0.31f, v = 0.17f;
        float previousMax = 0.0f;
        for (float gamma : {1.0f, 2.0f, 4.0f, 8.0f, 16.0f})
        {
            const auto hb = terrain::hexTerrainComputeBlend(u, v, 1.0f, gamma, 0.0f);
            const float maxW = std::max({hb.tap[0].weight, hb.tap[1].weight, hb.tap[2].weight});
            CAPTURE(gamma);
            CHECK(maxW >= previousMax);
            previousMax = maxW;
        }
        // By gamma 4 the dominant tap should already carry most of the blend, which is what keeps
        // the composite from looking like three overlaid copies.
        const auto hb4 = terrain::hexTerrainComputeBlend(u, v, 1.0f, 4.0f, 0.0f);
        CHECK(std::max({hb4.tap[0].weight, hb4.tap[1].weight, hb4.tap[2].weight}) > 0.5f);
    }

    // ------------------------------------------------------------------
    // 3. The blend: convex, therefore un-clippable.
    // ------------------------------------------------------------------

    TEST_CASE("the albedo blend never leaves the hull of its three taps")
    {
        // "No ghosting / contrast-loss artifacts" in executable form - and, more importantly, the
        // guarantee that the bake cannot write a clamped colour into an sRGB8 page. Note this holds
        // even though the luminance term reweights the taps: it only reweights, it never scales the
        // result the way a variance-preserving operator would.
        uint32_t rng = 12345u;
        auto next = [&rng]() {
            rng = rng * 1664525u + 1013904223u;
            return static_cast<float>((rng >> 8) & 0xFFFFu) / 65535.0f;
        };

        for (int iter = 0; iter < 400; ++iter)
        {
            const float u = next() * 8.0f - 4.0f;
            const float v = next() * 8.0f - 4.0f;
            const auto hb = terrain::hexTerrainComputeBlend(u, v, 1.0f, 4.0f, 0.0f);

            float c0[3], c1[3], c2[3], out[3];
            for (int ch = 0; ch < 3; ++ch)
            {
                c0[ch] = next();
                c1[ch] = next();
                c2[ch] = next();
            }
            terrain::hexTerrainCombineAlbedo(hb, c0, c1, c2, out);

            for (int ch = 0; ch < 3; ++ch)
            {
                const float lo = std::min({c0[ch], c1[ch], c2[ch]});
                const float hi = std::max({c0[ch], c1[ch], c2[ch]});
                CAPTURE(iter);
                CAPTURE(ch);
                CHECK(out[ch] >= lo - 1e-5f);
                CHECK(out[ch] <= hi + 1e-5f);
                CHECK(out[ch] >= 0.0f);
                CHECK(out[ch] <= 1.0f);
            }
        }
    }

    TEST_CASE("three identical taps blend to exactly that value")
    {
        // The degenerate case has to be exact, or a perfectly flat texture would acquire a faint
        // hexagonal ripple purely from the blend.
        const auto hb = terrain::hexTerrainComputeBlend(1.234f, 5.678f, 1.0f, 4.0f, 0.0f);
        const float c[3] = {0.25f, 0.5f, 0.75f};
        float out[3];
        terrain::hexTerrainCombineAlbedo(hb, c, c, c, out);
        CHECK(out[0] == doctest::Approx(0.25f));
        CHECK(out[1] == doctest::Approx(0.5f));
        CHECK(out[2] == doctest::Approx(0.75f));
    }

    // ------------------------------------------------------------------
    // 4. Rotation.
    // ------------------------------------------------------------------

    TEST_CASE("rotation 0 is a pure per-cell translation")
    {
        // Mikkelsen's own demo ships rotation off, so this is the default path and it must reduce
        // to "sample the same exemplar at an offset". Approximate rather than exact by design: the
        // GLSL renormalizes through inversesqrt, which the spec allows 2.5 ULP of error. Unlike
        // VK-1609's contrast, exactness is not load-bearing here - nothing downstream multiplies
        // this by zero.
        for (int i = 0; i < 25; ++i)
        {
            const float u = static_cast<float>(i) * 0.17f;
            const float v = static_cast<float>(i) * 0.29f;
            const auto hb = terrain::hexTerrainComputeBlend(u, v, 1.0f, 4.0f, 0.0f);
            for (const auto& t : hb.tap)
            {
                CAPTURE(u);
                CHECK(t.cos == doctest::Approx(1.0f).epsilon(1e-5));
                CHECK(t.sin == doctest::Approx(0.0f).epsilon(1e-5));
                // Offsets are in [0,1): the exemplar repeats with period 1, so any translation is
                // a valid re-tiling.
                CHECK(t.u - u >= -1e-4f);
                CHECK(t.u - u <= 1.0f + 1e-4f);
                CHECK(t.v - v >= -1e-4f);
                CHECK(t.v - v <= 1.0f + 1e-4f);
            }
        }
    }

    TEST_CASE("a rotated tap stays a unit rotation at every strength")
    {
        // The lerp toward identity is renormalized precisely so intermediate strengths are a pure
        // rotation rather than a rotation-plus-shrink, which would degenerate to zero at t = 0.5
        // for a 180-degree pick and scale that cell's UV to a point.
        for (float rot : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
        {
            for (int i = 0; i < 50; ++i)
            {
                const float u = static_cast<float>(i) * 0.37f - 9.0f;
                const auto hb = terrain::hexTerrainComputeBlend(u, u * 0.61f, 1.0f, 4.0f, rot);
                for (const auto& t : hb.tap)
                {
                    CAPTURE(rot);
                    CAPTURE(u);
                    CHECK(std::hypot(t.cos, t.sin) == doctest::Approx(1.0f).epsilon(1e-4));
                }
            }
        }
    }

    TEST_CASE("un-rotating a normal is the exact inverse of the UV rotation")
    {
        // The one piece of this design derived rather than copied: a tap fetched at a UV rotated by
        // R carries a tangent-space normal expressed in a basis rotated by R, so it must be brought
        // back by R TRANSPOSE. Getting the direction wrong would double the rotation instead of
        // cancelling it, and terrain bumps would swirl as the camera moved.
        const float angle = 0.7f;
        const float c = std::cos(angle);
        const float s = std::sin(angle);

        // A normal already in the surface frame, pushed into a tap's rotated frame by R^T, must
        // come back out unchanged.
        const float surface[3] = {0.3f, -0.6f, 0.74f};
        const float inTap[3] = {c * surface[0] - s * surface[1],
                                s * surface[0] + c * surface[1],
                                surface[2]};
        float back[3];
        terrain::hexTerrainUnrotateNormal(c, s, inTap, back);
        CHECK(back[0] == doctest::Approx(surface[0]));
        CHECK(back[1] == doctest::Approx(surface[1]));
        CHECK(back[2] == doctest::Approx(surface[2]));

        // A flat normal is unaffected by any rotation, which is the case that would hide a sign
        // error if it were the only one tested.
        const float flat[3] = {0.0f, 0.0f, 1.0f};
        float flatBack[3];
        terrain::hexTerrainUnrotateNormal(c, s, flat, flatBack);
        CHECK(flatBack[0] == doctest::Approx(0.0f));
        CHECK(flatBack[1] == doctest::Approx(0.0f));
        CHECK(flatBack[2] == doctest::Approx(1.0f));
    }

    // ------------------------------------------------------------------
    // 5. The resolver gate: what makes the feature cost nothing when unused.
    // ------------------------------------------------------------------

    TEST_CASE("resolveTerrainLayerPBR gates hex tiling on opt-in AND an albedo texture")
    {
        render::mesh::ExtractedPBRValues pbr;
        pbr.albedoTexturePath = "grass_albedo.vfImage";

        SUBCASE("a layer that did not opt in uploads exactly 0")
        {
            terrain::TerrainMaterialLayer layer;
            layer.hexTiling = false;
            const auto r = resolveTerrainLayerPBR(layer, &pbr);
            CHECK(r.hexTilingStrength == 0.0f);
            // The other scalars go to 0 as well, so a stale authored value cannot leak into a
            // shader arm that reads them.
            CHECK(r.hexCellScale == 0.0f);
            CHECK(r.hexContrast == 0.0f);
            CHECK(r.hexRotationStrength == 0.0f);
        }

        SUBCASE("opting in with an albedo texture uploads strength 1")
        {
            terrain::TerrainMaterialLayer layer;
            layer.hexTiling = true;
            const auto r = resolveTerrainLayerPBR(layer, &pbr);
            CHECK(r.hexTilingStrength == 1.0f);
            CHECK(r.hexContrast == doctest::Approx(terrain::HEX_TILING_DEFAULT_CONTRAST));
        }

        SUBCASE("opting in WITHOUT an albedo texture resolves to off")
        {
            // Such a layer composites a constant vec3(0.5); stochastically re-tiling a constant is
            // three fetches that cannot change the result.
            terrain::TerrainMaterialLayer layer;
            layer.hexTiling = true;
            const auto r = resolveTerrainLayerPBR(layer, nullptr);
            CHECK(r.albedoPath.empty());
            CHECK(r.hexTilingStrength == 0.0f);
        }

        SUBCASE("the contrast is clamped into the sharpening-safe range")
        {
            // Below 1 the exponent FLATTENS the weights instead of sharpening them, which puts the
            // ghosting back; above 16 the transition band goes sub-pixel and the cell edges alias.
            terrain::TerrainMaterialLayer layer;
            layer.hexTiling = true;

            layer.hexContrast = 0.0f;
            CHECK(resolveTerrainLayerPBR(layer, &pbr).hexContrast
                  == doctest::Approx(terrain::MIN_HEX_TILING_CONTRAST));

            layer.hexContrast = 1000.0f;
            CHECK(resolveTerrainLayerPBR(layer, &pbr).hexContrast
                  == doctest::Approx(terrain::MAX_HEX_TILING_CONTRAST));
        }

        SUBCASE("cell scale and rotation are clamped too")
        {
            terrain::TerrainMaterialLayer layer;
            layer.hexTiling = true;
            layer.hexCellScale = 0.0f;
            layer.hexRotation = 5.0f;
            const auto r = resolveTerrainLayerPBR(layer, &pbr);
            CHECK(r.hexCellScale == doctest::Approx(terrain::MIN_HEX_TILING_CELL_SCALE));
            CHECK(r.hexCellScale > 0.0f); // cellScale divides in the shader
            CHECK(r.hexRotationStrength == doctest::Approx(terrain::MAX_HEX_TILING_ROTATION));
        }
    }

    TEST_CASE("the permutation is driven by the resolved strengths, not the authored flags")
    {
        std::vector<ResolvedTerrainLayerPBR> layers(3);
        CHECK_FALSE(terrainMaterialWantsHexTiling(layers));
        layers[1].hexTilingStrength = 1.0f;
        CHECK(terrainMaterialWantsHexTiling(layers));
    }

    // ------------------------------------------------------------------
    // 6. What the emitter emits.
    // ------------------------------------------------------------------

    TEST_CASE("the hex arm triples albedo and normal fetches and leaves ORM alone")
    {
        for (bool detail : {false, true})
        {
            CAPTURE(detail);
            const std::string base = buildTerrainCompositeLoop(detail, /*heightBlend=*/false);
            const std::string hex = buildTerrainCompositeLoop(detail, /*heightBlend=*/false,
                                                              /*distanceRescale=*/false,
                                                              /*hexTiling=*/true);

            // One hex sampler call per map, each of which is three textureGrads inside the helper.
            CHECK(countOccurrences(hex, "hexTerrainSampleAlbedo(") == 1u);
            CHECK(countOccurrences(hex, "hexTerrainSampleNormal(") == (detail ? 1u : 0u));
            CHECK(countOccurrences(hex, "hexTerrainComputeBlend(") == 1u);

            // ORM stays SINGLE-tap. Under TERRAIN_HEIGHT_BLEND its alpha IS VK-1609's height
            // field; blending it across three cells would blur the height and fight the very
            // sharpening that story added. Emission is single-tap because it is rare and usually 0.
            CHECK(countOccurrences(hex, "nonuniformEXT(ormIdx)") == 1u);
            if (detail)
                CHECK(countOccurrences(hex, "nonuniformEXT(emissionIdx)") == 1u);

            // The per-layer opt-in is a ternary, not an `if`: GLSL evaluates only the taken
            // operand, so an opted-out layer pays one compare rather than three fetches - and the
            // composite's branch count, which the explicit-gradient contract rests on, is
            // unchanged. This is also why the AC's literal "blend factor 0/1" reading was not
            // taken: a mix() would run all three taps regardless.
            CHECK(hex.find("hexStrength > 0.0") != std::string::npos);
            CHECK(countOccurrences(hex, "if (") == countOccurrences(base, "if ("));
            CHECK(countOccurrences(hex, "continue;") == countOccurrences(base, "continue;"));

            // The blend is fed the layer's own gradients, computed by the includer in uniform flow.
            CHECK(hex.find("hexTerrainComputeBlend(layerUV, layerUVdx, layerUVdy") != std::string::npos);

            // ...and nothing leaks into the arm that did not ask for it.
            CHECK(base.find("hexTerrain") == std::string::npos);
            CHECK(base.find("hexStrength") == std::string::npos);
        }
    }

    TEST_CASE("with rescale on as well, the far tap hex-tiles too")
    {
        // Mixing a hex-sampled near tap toward a PLAIN far tap would make the layer revert to
        // visibly repeating sampling exactly where repetition is worst.
        const std::string both = buildTerrainCompositeLoop(/*detail=*/true, /*heightBlend=*/true,
                                                           /*distanceRescale=*/true, /*hexTiling=*/true);
        CHECK(countOccurrences(both, "hexTerrainComputeBlend(") == 2u);
        CHECK(countOccurrences(both, "hexTerrainSampleAlbedo(") == 2u);
        CHECK(both.find("hexTerrainComputeBlend(layerFarUV, layerFarUVdx, layerFarUVdy")
              != std::string::npos);
        // The normal is not distance-rescaled, so it keeps exactly one blend.
        CHECK(countOccurrences(both, "hexTerrainSampleNormal(") == 1u);
    }

    TEST_CASE("both includers pull in the hex helper at file scope, under the permutation macro")
    {
        // The generated composite is #include-d INSIDE main(), so it cannot declare functions; the
        // helper has to be a file-scope include in each consumer. It must also come AFTER the
        // bindlessTextures declaration, because it indexes that array rather than taking a
        // sampler2D parameter.
        const fs::path shaders = repoRoot() / "resources" / "shaders" / "gpudriven";
        for (const char* file : {"mesh_terrain.glsl", "terrain_rvt_bake.glsl"})
        {
            CAPTURE(file);
            const std::string src = repo_scan::readFile(shaders / file);
            REQUIRE_FALSE(src.empty());

            const size_t include = src.find("hex_tiling_terrain.glsl");
            const size_t bindless = src.find("uniform sampler2D bindlessTextures[]");
            const size_t guard = src.find("#ifdef TERRAIN_HEX_TILING");
            REQUIRE(include != std::string::npos);
            REQUIRE(bindless != std::string::npos);
            REQUIRE(guard != std::string::npos);
            CHECK_MESSAGE(bindless < include, "hex helper included before bindlessTextures exists");
            CHECK_MESSAGE(guard < include, "hex helper is not behind TERRAIN_HEX_TILING");
        }
    }

    TEST_CASE("the terrain hex helper does not inherit the ocean's skew or variance scale")
    {
        // A regression guard with a specific target: someone consolidating the two hex headers
        // would most naturally keep the older, ocean one. Its skew is transposed (scalene lattice)
        // and its varianceScale is variance-preserving about zero, which on albedo overshoots into
        // a clamped, permanently baked RVT page.
        const fs::path path = repoRoot() / "resources" / "shaders" / "common" / "hex_tiling_terrain.glsl";
        const std::string src = repo_scan::readFile(path);
        REQUIRE_FALSE(src.empty());

        // Search the CODE, not the prose. The file's header comment names both rejected techniques
        // in order to explain why they were rejected, so a raw substring search finds them and the
        // guard would fire on the documentation it is meant to protect.
        const std::string code = stripLineComments(src);

        CHECK_MESSAGE(code.find("st.x - 0.57735027 * st.y") != std::string::npos,
                      "equilateral skew is gone - check against water/hex_tiling.glsl's transposed form");
        CHECK_MESSAGE(code.find("varianceScale") == std::string::npos,
                      "variance-preserving blend is back; on albedo it overshoots into a clamped sRGB8 page");
        CHECK(code.find("inversesqrt(max(dot(w, w)") == std::string::npos);
        // Integer hash, never fract(sin(...)): terrain UVs grow without bound with world size.
        CHECK(code.find("0x7feb352du") != std::string::npos);
        CHECK(code.find("fract(sin") == std::string::npos);
        // The prose that explains both choices must survive too - it is the only place the water
        // file's lattice bug is written down.
        CHECK(src.find("varianceScale") != std::string::npos);
    }
}
