#include <doctest.h>

#include "test_repo_scan_helpers.hpp"

#include <terrain/TerrainWeatherResponse.hpp>
#include <terrain/TerrainSurfaceMaskAsset.hpp>
#include <terrain/SurfaceMaskBrushApplicator.hpp>
#include <terrain/TerrainMaterialTypes.hpp>
#include "render/gpudriven/GPUDrivenTypes.hpp"
#include "render/gpudriven/terrain/TerrainLayerPBRResolver.hpp"

#include <export/ShaderCompiler.hpp>
#include <resource/ShaderResource.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// VK-1614 terrain local wetness / snow.
//
// CPU-only: no Vulkan device, no window. shaderCompiler::compile is plain shaderc, so the GLSL half
// is still reachable here — which matters because every shader edit this story made lands in
// mesh_terrain.glsl, and its FRAGMENT stage is the only stage the suite can compile.

namespace
{
    namespace fs = std::filesystem;

    fs::path repoRoot()
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");
        return *root;
    }

    // Bitwise float comparison. `==` is not enough for the identity claims below: the whole point is
    // that the result is the SAME VALUE, not merely an equal one, so that a shader taking this path
    // produces byte-identical output to one that never had the feature.
    bool bitwiseEqual(float a, float b)
    {
        uint32_t ua, ub;
        std::memcpy(&ua, &a, sizeof(ua));
        std::memcpy(&ub, &b, sizeof(ub));
        return ua == ub;
    }
}

TEST_SUITE("TerrainWeatherResponse")
{
    // ------------------------------------------------------------------
    // 1. The identity properties the "masks off = bit-exact" AC rests on.
    // ------------------------------------------------------------------

    TEST_CASE("an unauthored material resolves to the neutral default, bitwise")
    {
        using namespace terrain;

        // authSum == 0 is what the shader's step(eps, scalar) accumulator produces when no layer
        // covering a fragment opted in. mix(x, y, 0.0) must then return x unchanged — if it merely
        // returned something close to x, every terrain material in the project would shift the day
        // this permutation was compiled in.
        for (float defaultValue : {0.0f, 0.25f, 0.81f, 1.0f})
        {
            CAPTURE(defaultValue);
            const float r = blendWeatherResponse(/*sum=*/0.0f, /*authSum=*/0.0f,
                                                 /*invW=*/1000.0f, defaultValue);
            CHECK(bitwiseEqual(r, defaultValue));
        }

        // ... and it must stay finite even though the divisor is zero: the max() guard is what stops
        // a NaN from propagating through the mix and poisoning the "authored 0" case.
        const float finite = blendWeatherResponse(0.0f, 0.0f, 1000.0f, 1.0f);
        CHECK(std::isfinite(finite));
    }

    TEST_CASE("a fully-authored fragment uses the authored value")
    {
        using namespace terrain;

        // One layer covering the whole fragment at porosity 0.2: authSum == totalW, so t == 1 and
        // the derived default is fully replaced.
        const float w = 0.8f;
        const float invW = 1.0f / (std::max)(w, 0.001f);
        const float r = blendWeatherResponse(/*sum=*/0.2f * w, /*authSum=*/w, invW, /*default=*/0.81f);
        CHECK(r == doctest::Approx(0.2f));
    }

    TEST_CASE("a partly-authored fragment interpolates and stays monotone")
    {
        using namespace terrain;

        // Half the fragment is an authored layer (porosity 0.1), half is unauthored.
        const float wAuthored = 0.5f;
        const float totalW = 1.0f;
        const float invW = 1.0f / totalW;
        const float derived = 0.81f;

        const float r = blendWeatherResponse(0.1f * wAuthored, wAuthored, invW, derived);
        CHECK(r == doctest::Approx(0.5f * derived + 0.5f * 0.1f));
        // Bracketed by the two endpoints — no overshoot, which is what keeps the response continuous
        // as an artist paints one layer over another.
        CHECK(r <= derived);
        CHECK(r >= 0.1f);

        float previous = derived;
        for (int i = 1; i <= 10; ++i)
        {
            const float cover = static_cast<float>(i) / 10.0f;
            const float v = blendWeatherResponse(0.1f * cover, cover, invW, derived);
            CHECK(v <= previous + 1e-6f);
            previous = v;
        }
    }

    TEST_CASE("combineWeatherSignal is exact at mask 0, saturating, and monotone")
    {
        using namespace terrain;

        // The property an absent or all-black .vfImage depends on.
        for (float g : {0.0f, 0.001f, 0.37f, 0.5f, 1.0f})
        {
            CAPTURE(g);
            CHECK(bitwiseEqual(combineWeatherSignal(g, 0.0f), g));
        }

        // Saturating BY CONSTRUCTION for inputs in [0, 1] — no clamp instruction in the shader, so
        // nothing can perturb the identity above.
        for (float g = 0.0f; g <= 1.0f; g += 0.1f)
        {
            for (float m = 0.0f; m <= 1.0f; m += 0.1f)
            {
                const float v = combineWeatherSignal(g, m);
                CHECK(v >= -1e-6f);
                CHECK(v <= 1.0f + 1e-6f);
                CHECK(v >= g - 1e-6f); // can only ADD; suppression is the per-layer half's job
                CHECK(v >= m - 1e-6f);
            }
        }

        // The case max(g, m) gets wrong: a painted basin during rain must still read wetter than
        // the unpainted ground around it, or the crevice-puddle half of the story does nothing.
        const float basin = combineWeatherSignal(0.6f, 0.3f);
        CHECK(basin > 0.6f);
    }

    TEST_CASE("puddle coverage needs flat, concave, non-absorbent ground")
    {
        using namespace terrain;

        // Slope: nothing pools on a wall.
        CHECK(puddleCoverage(1.0f, /*upDotY=*/0.5f, /*ao=*/0.0f, /*porosity=*/0.0f) == doctest::Approx(0.0f));
        CHECK(puddleCoverage(1.0f, PUDDLE_SLOPE_MIN, 0.0f, 0.0f) == doctest::Approx(0.0f));
        // Flat, fully concave, fully non-absorbent, soaking wet => full coverage.
        CHECK(puddleCoverage(1.0f, 1.0f, 0.0f, 0.0f) == doctest::Approx(1.0f));
        // Porosity is what stops sand from puddling.
        CHECK(puddleCoverage(1.0f, 1.0f, 0.0f, 1.0f) == doctest::Approx(0.0f));
        // Dry ground never puddles regardless of shape.
        CHECK(puddleCoverage(0.0f, 1.0f, 0.0f, 0.0f) == doctest::Approx(0.0f));

        // Range and monotonicity in each argument.
        for (float wet = 0.0f; wet <= 1.0f; wet += 0.25f)
            for (float up = 0.9f; up <= 1.0f; up += 0.02f)
                for (float ao = 0.0f; ao <= 1.0f; ao += 0.25f)
                    for (float por = 0.0f; por <= 1.0f; por += 0.25f)
                    {
                        const float v = puddleCoverage(wet, up, ao, por);
                        const bool inRange = v >= -1e-6f && v <= 1.0f + 1e-6f;
                        CHECK(inRange);
                    }

        // Wetter is never less puddled; more porous is never more puddled.
        CHECK(puddleCoverage(1.0f, 1.0f, 0.0f, 0.2f) >= puddleCoverage(0.5f, 1.0f, 0.0f, 0.2f));
        CHECK(puddleCoverage(1.0f, 1.0f, 0.0f, 0.2f) >= puddleCoverage(1.0f, 1.0f, 0.0f, 0.8f));
    }

    // ------------------------------------------------------------------
    // 2. The resolver seam: authored intent -> GPU scalar.
    // ------------------------------------------------------------------

    TEST_CASE("resolveLayerWeatherScalar reserves 0 as the opt-out sentinel")
    {
        using namespace terrain;

        // Opted out => EXACTLY 0, whatever the slider says. This is what the shader's
        // step(eps, value) reads, so anything else would silently enable the response.
        CHECK(bitwiseEqual(resolveLayerWeatherScalar(0.9f, false), 0.0f));
        CHECK(bitwiseEqual(resolveLayerWeatherScalar(0.0f, false), 0.0f));

        // Opted in, dragged to zero => clamped UP to the floor, never to the sentinel. An artist
        // asking for "absorbs nothing" must not accidentally mean "did not opt in".
        const float atZero = resolveLayerWeatherScalar(0.0f, true);
        CHECK(atZero > 0.0f);
        CHECK(atZero == doctest::Approx(MIN_LAYER_WEATHER_SCALAR));
        CHECK(resolveLayerWeatherScalar(-5.0f, true) == doctest::Approx(MIN_LAYER_WEATHER_SCALAR));
        CHECK(resolveLayerWeatherScalar(5.0f, true) == doctest::Approx(MAX_LAYER_WEATHER_SCALAR));
        CHECK(resolveLayerWeatherScalar(0.5f, true) == doctest::Approx(0.5f));
    }

    TEST_CASE("resolveTerrainLayerPBR gates both weather scalars on the layer's opt-in")
    {
        using render::gpudriven::resolveTerrainLayerPBR;
        using render::gpudriven::terrainMaterialWantsWeatherResponse;

        terrain::TerrainMaterialLayer layer;
        layer.porosity = 0.75f;
        layer.snowRetention = 0.25f;

        SUBCASE("opted out")
        {
            layer.weatherResponse = false;
            const auto r = resolveTerrainLayerPBR(layer, nullptr);
            CHECK(bitwiseEqual(r.porosity, 0.0f));
            CHECK(bitwiseEqual(r.snowRetention, 0.0f));
            CHECK_FALSE(terrainMaterialWantsWeatherResponse({r}));
        }

        SUBCASE("opted in")
        {
            layer.weatherResponse = true;
            const auto r = resolveTerrainLayerPBR(layer, nullptr);
            CHECK(r.porosity == doctest::Approx(0.75f));
            CHECK(r.snowRetention == doctest::Approx(0.25f));
            CHECK(terrainMaterialWantsWeatherResponse({r}));
        }

        SUBCASE("no texture precondition — unlike hex tiling and height blend")
        {
            // resolveTerrainLayerPBR forces hexTilingStrength to 0 without an albedo and
            // heightBlendContrast to 0 without an ORM. The weather response has no such gate: it
            // modulates shading every layer receives, textured or not.
            layer.weatherResponse = true;
            const auto r = resolveTerrainLayerPBR(layer, nullptr);
            CHECK(r.albedoPath.empty());
            CHECK(bitwiseEqual(r.hexTilingStrength, 0.0f));
            CHECK(bitwiseEqual(r.heightBlendContrast, 0.0f));
            CHECK(r.porosity > 0.0f);
        }
    }

    TEST_CASE("a zero-initialised layer slot reads NEUTRAL, not 'sheds everything'")
    {
        using render::gpudriven::TerrainLayerGPUData;

        // Per-tile palette indices may legitimately exceed activeLayerCount, so a real fragment can
        // index a slot that was never filled. Had 1.0 meant "retains snow", those slots — and every
        // pre-VK-1614 material — would have silently lost all snow the day the field was read.
        const TerrainLayerGPUData zeroed{};
        CHECK(bitwiseEqual(zeroed.layerPorosity, 0.0f));
        CHECK(bitwiseEqual(zeroed.layerSnowRetention, 0.0f));

        // Which the accumulator resolves back to the neutral defaults.
        CHECK(bitwiseEqual(terrain::blendWeatherResponse(zeroed.layerSnowRetention * 1.0f,
                                                         0.0f, 1.0f, 1.0f), 1.0f));
    }

    // ------------------------------------------------------------------
    // 3. The mask asset.
    // ------------------------------------------------------------------

    TEST_CASE(".vfImage surface mask survives a save/load round trip")
    {
        using namespace terrain;

        auto mask = TerrainSurfaceMaskAsset::createEmpty(SURFACE_MASK_MIN_RESOLUTION);
        REQUIRE(mask);
        REQUIRE(mask->isValid());
        CHECK(mask->width == SURFACE_MASK_MIN_RESOLUTION);

        // A fresh mask must be all-zero in the colour channels: that is the "no local contribution"
        // value combineWeatherSignal turns into a bitwise no-op.
        CHECK(mask->getChannel(0, 0, SURFACE_MASK_WETNESS_CHANNEL) == doctest::Approx(0.0f));
        CHECK(mask->getChannel(5, 7, SURFACE_MASK_SNOW_CHANNEL) == doctest::Approx(0.0f));

        mask->setChannel(3, 4, SURFACE_MASK_WETNESS_CHANNEL, 1.0f);
        mask->setChannel(3, 4, SURFACE_MASK_SNOW_CHANNEL, 0.5f);
        mask->setChannel(10, 2, SURFACE_MASK_SNOW_CHANNEL, 1.0f);

        const fs::path out = fs::temp_directory_path() / "vk1614_surface_mask_roundtrip.vfImage";
        std::error_code ec;
        fs::remove(out, ec);

        REQUIRE(TerrainSurfaceMaskAsset::save(out.string(), *mask));
        auto loaded = TerrainSurfaceMaskAsset::load(out.string());
        REQUIRE(loaded);
        REQUIRE(loaded->isValid());

        CHECK(loaded->width == mask->width);
        CHECK(loaded->height == mask->height);
        // Byte-for-byte: the BGRA swizzle has to be its own inverse or wetness and snow swap places
        // with the reserved blue channel on every save.
        CHECK(loaded->rgba == mask->rgba);
        CHECK(loaded->getChannel(3, 4, SURFACE_MASK_WETNESS_CHANNEL) == doctest::Approx(1.0f));
        CHECK(loaded->getChannel(3, 4, SURFACE_MASK_SNOW_CHANNEL) == doctest::Approx(0.5f).epsilon(0.01));
        CHECK(loaded->getChannel(10, 2, SURFACE_MASK_SNOW_CHANNEL) == doctest::Approx(1.0f));

        // Alpha must be 255 EVERYWHERE. HeightmapLoader treats any non-255 alpha in an uncompressed
        // .vfImage as "this is a 16-bit heightmap, RGB is the high byte and A the low byte", so a
        // varying alpha here would decode as garbage the moment someone pointed a stamp brush at it.
        bool allOpaque = true;
        for (size_t i = 3; i < loaded->rgba.size(); i += SURFACE_MASK_CHANNELS)
        {
            if (loaded->rgba[i] != 255) { allOpaque = false; break; }
        }
        CHECK(allOpaque);

        fs::remove(out, ec);
    }

    TEST_CASE("surface mask load rejects implausible files instead of allocating from them")
    {
        // The .vfImage format has no magic number, so the dimensions are the only guard against
        // parsing an unrelated file as a mask.
        const fs::path junk = fs::temp_directory_path() / "vk1614_not_a_mask.vfImage";
        {
            std::ofstream f(junk, std::ios::binary);
            const std::vector<char> noise(256, '\x7f');
            f.write(noise.data(), static_cast<std::streamsize>(noise.size()));
        }
        CHECK(terrain::TerrainSurfaceMaskAsset::load(junk.string()) == nullptr);
        CHECK(terrain::TerrainSurfaceMaskAsset::load("does_not_exist.vfImage") == nullptr);

        std::error_code ec;
        fs::remove(junk, ec);
    }

    // ------------------------------------------------------------------
    // 4. The brush.
    // ------------------------------------------------------------------

    TEST_CASE("the surface-mask brush paints, erases, and stays inside the authored rect")
    {
        using namespace terrain;

        auto mask = TerrainSurfaceMaskAsset::createEmpty(SURFACE_MASK_MIN_RESOLUTION);
        REQUIRE(mask);

        SurfaceMaskBrushApplicator::ApplyParams params;
        params.maskWorldRect = glm::vec4(0.0f, 0.0f, 64.0f, 64.0f);
        params.brushCenter = glm::vec2(32.0f, 32.0f);
        params.brushRadius = 8.0f;
        params.brushStrength = 10.0f;
        params.brushOpacity = 1.0f;
        params.falloff = BrushFalloff::Constant;
        params.channel = SURFACE_MASK_WETNESS_CHANNEL;
        params.deltaTime = 1.0f;

        const auto dirty = SurfaceMaskBrushApplicator::apply(*mask, params);
        REQUIRE_FALSE(dirty.isEmpty());

        // Centre painted, and the OTHER channel untouched — wetness and snow are independent
        // signals, not a partition of unity like the splat weights.
        const uint32_t cx = mask->width / 2;
        CHECK(mask->getChannel(cx, cx, SURFACE_MASK_WETNESS_CHANNEL) > 0.9f);
        CHECK(mask->getChannel(cx, cx, SURFACE_MASK_SNOW_CHANNEL) == doctest::Approx(0.0f));

        // The dirty rect must bound the stroke, so a caller can trust it to size an upload.
        CHECK(dirty.minX <= cx);
        CHECK(dirty.maxX >= cx);
        // ... and be far tighter than the whole image, which is the point of tracking it.
        CHECK(dirty.maxX - dirty.minX < mask->width);

        // A corner well outside an 8 m brush centred at (32, 32) must be untouched.
        CHECK(mask->getChannel(0, 0, SURFACE_MASK_WETNESS_CHANNEL) == doctest::Approx(0.0f));

        SUBCASE("invert erases")
        {
            params.invert = true;
            const auto erased = SurfaceMaskBrushApplicator::apply(*mask, params);
            CHECK_FALSE(erased.isEmpty());
            CHECK(mask->getChannel(cx, cx, SURFACE_MASK_WETNESS_CHANNEL) == doctest::Approx(0.0f));
        }

        SUBCASE("a stroke entirely outside the rect is a no-op")
        {
            params.brushCenter = glm::vec2(1000.0f, 1000.0f);
            auto fresh = TerrainSurfaceMaskAsset::createEmpty(SURFACE_MASK_MIN_RESOLUTION);
            const auto none = SurfaceMaskBrushApplicator::apply(*fresh, params);
            CHECK(none.isEmpty());
        }

        SUBCASE("a degenerate rect is refused rather than dividing by ~0")
        {
            params.maskWorldRect = glm::vec4(5.0f, 5.0f, 5.0f, 5.0f);
            auto fresh = TerrainSurfaceMaskAsset::createEmpty(SURFACE_MASK_MIN_RESOLUTION);
            const auto none = SurfaceMaskBrushApplicator::apply(*fresh, params);
            CHECK(none.isEmpty());
        }
    }

    TEST_CASE("paint targets map to the documented mask channels")
    {
        CHECK(terrain::paintTargetToMaskChannel(terrain::PaintTarget::Wetness)
              == terrain::SURFACE_MASK_WETNESS_CHANNEL);
        CHECK(terrain::paintTargetToMaskChannel(terrain::PaintTarget::Snow)
              == terrain::SURFACE_MASK_SNOW_CHANNEL);
        CHECK(terrain::SURFACE_MASK_WETNESS_CHANNEL != terrain::SURFACE_MASK_SNOW_CHANNEL);
    }

    // ------------------------------------------------------------------
    // 5. The shader contract.
    // ------------------------------------------------------------------

    TEST_CASE("the weather block is live-only: the RVT bake must never see it")
    {
        // This is the executable form of "baked pages stay weather-independent". If the bake ever
        // #includes terrain_weather.glsl, every resident page starts carrying a weather state and
        // has to be re-baked when the weather changes — and the permutation would have to move into
        // TerrainCompositePermutation, doubling the generated composite from 16 arms to 32.
        const fs::path shaders = repoRoot() / "resources" / "shaders";
        const std::string live = repo_scan::readFile(shaders / "gpudriven" / "mesh_terrain.glsl");
        const std::string bake = repo_scan::readFile(shaders / "gpudriven" / "terrain_rvt_bake.glsl");
        REQUIRE_FALSE(live.empty());
        REQUIRE_FALSE(bake.empty());

        CHECK(live.find("terrain_weather.glsl") != std::string::npos);
        CHECK(bake.find("terrain_weather.glsl") == std::string::npos);

        for (const char* macro : {"TERRAIN_WEATHER_RESPONSE", "TERRAIN_WEATHER_MASK", "TERRAIN_PUDDLES"})
        {
            CAPTURE(macro);
            CHECK(bake.find(macro) == std::string::npos);
        }

        // The mask rides set 11, the one set the bake pipeline does not bind at all. (VK-1611's
        // anti-tiling block had to go on the weight-map set for exactly the opposite reason.)
        CHECK(live.find("set = 11, binding = 5) uniform sampler2D terrainWeatherMaskTexture")
              != std::string::npos);
        CHECK(live.find("set = 11, binding = 6) uniform TerrainWeatherMaskUBO") != std::string::npos);
    }

    TEST_CASE("with local weather off, mesh_terrain still makes the two original calls verbatim")
    {
        // The "masks off = today's output bit-exact" AC, pinned as text. The #else arm is what a
        // build with no mask and no authored layer compiles, so it must stay character-for-character
        // the two lines that shipped before this story.
        const fs::path shaders = repoRoot() / "resources" / "shaders" / "gpudriven";
        const std::string live = repo_scan::readFile(shaders / "mesh_terrain.glsl");
        REQUIRE_FALSE(live.empty());

        CHECK(live.find("    applyWetness(camera.wetness, albedo, roughness, metallic, N);")
              != std::string::npos);
        CHECK(live.find("    applySnowAccumulation(camera.snowAccumulation, fragNormal, albedo, "
                        "roughness, metallic, N);") != std::string::npos);
    }

    TEST_CASE("common/wetness.glsl and snow_accumulation.glsl are untouched by this story")
    {
        // Both are #include-d by mesh_shader_gpudriven.glsl (static meshes). The only way to claim
        // static-mesh output is unperturbed is to leave their token stream alone — hence the
        // deliberate five-statement duplication in applyTerrainWetness, pinned below.
        const fs::path common = repoRoot() / "resources" / "shaders" / "common";
        const std::string wetness = repo_scan::readFile(common / "wetness.glsl");
        const std::string weather = repo_scan::readFile(common / "terrain_weather.glsl");
        REQUIRE_FALSE(wetness.empty());
        REQUIRE_FALSE(weather.empty());

        // wetness.glsl still derives porosity itself and exposes exactly the original signature.
        CHECK(wetness.find("float porosity = clamp(roughness * roughness, 0.0, 1.0);")
              != std::string::npos);
        CHECK(wetness.find("void applyWetness(float wetness, inout vec3 albedo, inout float roughness,")
              != std::string::npos);
        // No terrain-only symbol leaked into the shared file.
        CHECK(wetness.find("porosity,") == std::string::npos);

        // The duplicated statements must still match wetness.glsl's, or the two responses drift.
        for (const char* stmt : {
                 "albedo *= mix(1.0, 0.6, wetness * porosity);",
                 "roughness = mix(roughness, roughness * 0.3, wetness);",
                 "metallic = mix(metallic, max(metallic, 0.02), wetness);",
                 "N = normalize(mix(N, vec3(0.0, 1.0, 0.0), wetness * 0.3));"})
        {
            CAPTURE(stmt);
            CHECK(wetness.find(stmt) != std::string::npos);
            CHECK(weather.find(stmt) != std::string::npos);
        }

        // snow_accumulation.glsl needs no change at all: retention folds into the amount, because
        // snowFactor = amount * slopeMask makes (amount * retention) * slopeMask equivalent.
        const std::string snow = repo_scan::readFile(common / "snow_accumulation.glsl");
        REQUIRE_FALSE(snow.empty());
        CHECK(snow.find("void applySnowAccumulation(float snowAmount, vec3 worldNormal,")
              != std::string::npos);
        CHECK(snow.find("retention") == std::string::npos);
    }

    TEST_CASE("the shoreline block no longer applies a second, contradictory material model")
    {
        // VK-1614 folded it into the single wetness signal. It used to raise roughness with
        // max(roughness, shoreWetRoughness) immediately before applyWetness multiplied roughness by
        // 0.3 — two models pulling opposite ways, and the max() was a no-op at the shipped defaults
        // (0.15 against a 0.9 terrain layer roughness) so the slider did nothing across its range.
        const fs::path shaders = repoRoot() / "resources" / "shaders" / "gpudriven";
        const std::string live = repo_scan::readFile(shaders / "mesh_terrain.glsl");
        const std::string mesh = repo_scan::readFile(shaders / "mesh_shader_gpudriven.glsl");
        REQUIRE_FALSE(live.empty());
        REQUIRE_FALSE(mesh.empty());

        CHECK(live.find("causticParams.shoreWetDarkening") == std::string::npos);
        CHECK(live.find("causticParams.shoreWetRoughness") == std::string::npos);
        // The extent knob survives, and now feeds the shared signal.
        CHECK(live.find("causticParams.shoreWetRange") != std::string::npos);
        CHECK(live.find("terrainWeatherOr(twWet, shoreWet)") != std::string::npos);

        // Both shaders mirror render::water::CausticParams, so the retired slots must be padding in
        // both or the UBO desyncs between the terrain and static-mesh pipelines.
        for (const std::string* src : {&live, &mesh})
        {
            CHECK(src->find("shoreWetDarkening;") == std::string::npos);
            CHECK(src->find("shoreWetRoughness;") == std::string::npos);
        }
    }

    TEST_CASE("mesh_terrain's fragment stage compiles with the weather macros and caustics crossed")
    {
        // test_terrain_anti_tiling.cpp covers the weather macros against RVT and the composite
        // flags. This adds the one cross that needs a macro VALUE and so cannot live in that list:
        // CAUSTICS_ENABLED, which is where the shoreline now feeds the unified signal and which
        // supplies the `ao` and `roughness` the puddle term and the derived porosity read.
        const fs::path shaderDir = repoRoot() / "resources" / "shaders" / "gpudriven";
        const fs::path shaderPath = shaderDir / "mesh_terrain.glsl";
        REQUIRE_MESSAGE(fs::exists(shaderPath), "missing " << shaderPath.string());

        const auto stages = resource::ShaderResource::readShaderFile(shaderPath.string());
        REQUIRE_FALSE(stages.empty());

        const std::vector<std::vector<std::string>> configs = {
            {"CAUSTICS_ENABLED"},
            {"CAUSTICS_ENABLED", "TERRAIN_PUDDLES"},
            {"CAUSTICS_ENABLED", "TERRAIN_WEATHER_RESPONSE", "TERRAIN_WEATHER_MASK", "TERRAIN_PUDDLES"},
            {"CAUSTICS_ENABLED", "RVT_ENABLED", "TERRAIN_WEATHER_RESPONSE", "TERRAIN_WEATHER_MASK",
             "TERRAIN_PUDDLES"},
        };

        bool sawFragment = false;
        for (const auto& macros : configs)
        {
            shaderCompiler::CompileOptions options;
            options.includeBasePath = shaderDir;
            options.macroNames = macros;
            options.macroValues = {{"CAUSTIC_SET", "12"}};

            std::string label;
            for (const auto& m : macros) { label += "+"; label += m; }
            CAPTURE(label);

            for (const auto& stage : stages)
            {
                if (stage.type != resource::ShaderType::FRAGMENT)
                    continue;
                sawFragment = true;
                const auto vkStage =
                    shaderCompiler::shaderTypeToVulkanStage(static_cast<uint8_t>(stage.type));
                const auto spirv = shaderCompiler::compile(
                    stage.source, vkStage, "mesh_terrain.glsl:frag" + label, options);
                CHECK_MESSAGE(!spirv.empty(), "failed to compile mesh_terrain fragment" << label);
            }
        }
        CHECK(sawFragment);
    }
}
