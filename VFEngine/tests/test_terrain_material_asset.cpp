#include <doctest.h>
#include <terrain/TerrainMaterialAsset.hpp>
#include <terrain/TerrainMaterialTypes.hpp>
#include "render/gpudriven/terrain/TerrainLayerPBRResolver.hpp"
#include "render/material/MaterialPBRExtractor.hpp"
#include <asset/AssetRef.hpp>
#include <asset/AssetGUID.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

// VK-1486: terrain layers source PBR from a referenced .vfMat/.vfMatInstance and no longer carry
// inline texture/scalar fields. These tests cover (a) load/save of the material-only layer format,
// (b) that legacy .vfterrainmat files still parse, and (c) the pure PBR resolver.

namespace
{
    namespace fs = std::filesystem;

    asset::AssetRef makeRef()
    {
        return asset::AssetRef::fromGUID(asset::AssetGUID::generate());
    }

    fs::path tempPath(const std::string& name)
    {
        return fs::temp_directory_path() / name;
    }
}

TEST_SUITE("TerrainMaterialAsset")
{
    TEST_CASE("save/load round-trips materialRef, tiling, blend and enabled")
    {
        terrain::TerrainMaterialData mat;
        mat.uuid = "test-uuid";
        mat.name = "RoundTrip";
        mat.activeLayerCount = 2;

        mat.layers[0].name = "Ground";
        mat.layers[0].materialRef = makeRef();
        mat.layers[0].tilingScale = 4.0f;
        mat.layers[0].enabled = true;

        mat.layers[1].name = "Rock";
        mat.layers[1].materialRef = makeRef();
        mat.layers[1].tilingScale = 2.5f;
        mat.layers[1].blendMode = terrain::TerrainLayerBlendMode::HeightBlend;
        mat.layers[1].heightContrast = 7.25f;
        mat.layers[1].enabled = false;

        const auto path = tempPath("vf_test_terrainmat_roundtrip.vfterrainmat");
        REQUIRE(terrain::TerrainMaterialAsset::save(path.string(), mat));

        auto loaded = terrain::TerrainMaterialAsset::load(path.string());
        REQUIRE(loaded.has_value());

        CHECK(loaded->activeLayerCount == 2);

        CHECK(loaded->layers[0].name == "Ground");
        CHECK(loaded->layers[0].materialRef.isValid());
        CHECK(loaded->layers[0].materialRef == mat.layers[0].materialRef);
        CHECK(loaded->layers[0].tilingScale == doctest::Approx(4.0f));
        CHECK(loaded->layers[0].blendMode == terrain::TerrainLayerBlendMode::Linear);
        CHECK(loaded->layers[0].heightContrast == doctest::Approx(4.0f)); // VK-1609 default
        CHECK(loaded->layers[0].enabled == true);

        CHECK(loaded->layers[1].name == "Rock");
        CHECK(loaded->layers[1].materialRef == mat.layers[1].materialRef);
        CHECK(loaded->layers[1].tilingScale == doctest::Approx(2.5f));
        CHECK(loaded->layers[1].blendMode == terrain::TerrainLayerBlendMode::HeightBlend);
        CHECK(loaded->layers[1].heightContrast == doctest::Approx(7.25f));
        CHECK(loaded->layers[1].enabled == false);

        fs::remove(path);
    }

    TEST_CASE("VK-1611/VK-1612 anti-tiling settings round-trip")
    {
        terrain::TerrainMaterialData mat;
        mat.activeLayerCount = 1;
        mat.layers[0].name = "Ground";
        mat.layers[0].materialRef = makeRef();
        mat.layers[0].hexTiling = true;
        mat.layers[0].hexCellScale = 2.5f;
        mat.layers[0].hexContrast = 6.5f;
        mat.layers[0].hexRotation = 0.75f;

        mat.antiTiling.macroVariationStrength = 0.35f;
        mat.antiTiling.macroVariationSize0 = 300.0f;
        mat.antiTiling.macroVariationSize1 = 90.0f;
        mat.antiTiling.macroVariationSeed = 4242u;
        mat.antiTiling.distanceRescaleStrength = 0.8f;
        mat.antiTiling.distanceRescaleScale = 0.2f;
        mat.antiTiling.distanceRescaleKnee = -6.5f;
        mat.antiTiling.distanceRescaleWidth = 1.5f;

        const auto path = tempPath("vf_test_terrainmat_antitiling.vfterrainmat");
        REQUIRE(terrain::TerrainMaterialAsset::save(path.string(), mat));

        auto loaded = terrain::TerrainMaterialAsset::load(path.string());
        REQUIRE(loaded.has_value());

        CHECK(loaded->layers[0].hexTiling == true);
        CHECK(loaded->layers[0].hexCellScale == doctest::Approx(2.5f));
        CHECK(loaded->layers[0].hexContrast == doctest::Approx(6.5f));
        CHECK(loaded->layers[0].hexRotation == doctest::Approx(0.75f));

        CHECK(loaded->antiTiling.macroVariationStrength == doctest::Approx(0.35f));
        CHECK(loaded->antiTiling.macroVariationSize0 == doctest::Approx(300.0f));
        CHECK(loaded->antiTiling.macroVariationSize1 == doctest::Approx(90.0f));
        CHECK(loaded->antiTiling.macroVariationSeed == 4242u);
        CHECK(loaded->antiTiling.distanceRescaleStrength == doctest::Approx(0.8f));
        CHECK(loaded->antiTiling.distanceRescaleScale == doctest::Approx(0.2f));
        CHECK(loaded->antiTiling.distanceRescaleKnee == doctest::Approx(-6.5f));
        CHECK(loaded->antiTiling.distanceRescaleWidth == doctest::Approx(1.5f));

        fs::remove(path);
    }

    TEST_CASE("VK-1614 per-layer weather response round-trips")
    {
        terrain::TerrainMaterialData mat;
        mat.activeLayerCount = 2;
        mat.layers[0].name = "Rock";
        mat.layers[0].materialRef = makeRef();
        mat.layers[0].weatherResponse = true;
        mat.layers[0].porosity = 0.12f;
        mat.layers[0].snowRetention = 0.3f;
        mat.layers[1].name = "Sand"; // left opted out on purpose

        const auto path = tempPath("vf_test_terrainmat_weather.vfterrainmat");
        REQUIRE(terrain::TerrainMaterialAsset::save(path.string(), mat));

        auto loaded = terrain::TerrainMaterialAsset::load(path.string());
        REQUIRE(loaded.has_value());

        CHECK(loaded->layers[0].weatherResponse == true);
        CHECK(loaded->layers[0].porosity == doctest::Approx(0.12f));
        CHECK(loaded->layers[0].snowRetention == doctest::Approx(0.3f));

        // An untouched layer must come back at the neutral defaults, not at zero — the whole
        // sentinel scheme depends on "did not opt in" being distinguishable from "authored 0".
        CHECK(loaded->layers[1].weatherResponse == false);
        CHECK(loaded->layers[1].porosity == doctest::Approx(terrain::DEFAULT_LAYER_POROSITY));
        CHECK(loaded->layers[1].snowRetention == doctest::Approx(terrain::DEFAULT_LAYER_SNOW_RETENTION));

        fs::remove(path);
    }

    TEST_CASE("a .vfterrainmat written before VK-1614 loads with the response off")
    {
        // The additive-key claim, as an executable check: absent keys must read as "no weather
        // response", which is what keeps the permutation uncompiled and the shader bit-exact for
        // every material already on disk.
        const auto path = tempPath("vf_test_terrainmat_pre1614.vfterrainmat");
        {
            nlohmann::json j;
            j["version"] = terrain::TERRAIN_MATERIAL_FORMAT_VERSION;
            j["name"] = "Legacy";
            j["activeLayerCount"] = 1;
            nlohmann::json layer;
            layer["name"] = "Ground";
            layer["tilingScale"] = 1.0f;
            j["layers"] = nlohmann::json::array({layer});
            std::ofstream f(path);
            f << j.dump(2);
        }

        auto loaded = terrain::TerrainMaterialAsset::load(path.string());
        REQUIRE(loaded.has_value());
        CHECK(loaded->layers[0].weatherResponse == false);

        const auto resolved = render::gpudriven::resolveTerrainLayerPBR(loaded->layers[0], nullptr);
        CHECK(resolved.porosity == 0.0f);
        CHECK(resolved.snowRetention == 0.0f);
        CHECK_FALSE(render::gpudriven::terrainMaterialWantsWeatherResponse({resolved}));

        fs::remove(path);
    }

    TEST_CASE("out-of-range anti-tiling values are clamped on load, not passed through")
    {
        // The load path clamps as well as the resolver, so a hand-edited file cannot put an
        // infinite macro frequency or a zero-width smoothstep in front of the GPU even if it
        // somehow bypassed the editor's sliders.
        const auto path = tempPath("vf_test_terrainmat_clamp.vfterrainmat");
        {
            std::ofstream f(path);
            f << R"JSON({
  "version": "1.0",
  "name": "Clamped",
  "activeLayerCount": 1,
  "layers": [ { "name": "L0", "hexTiling": true, "hexContrast": 999.0, "hexCellScale": 0.0 } ],
  "antiTiling": {
    "macroVariationStrength": 5.0,
    "macroVariationSize0": 0.0,
    "distanceRescaleWidth": 0.0,
    "distanceRescaleScale": 99.0
  }
})JSON";
        }

        auto loaded = terrain::TerrainMaterialAsset::load(path.string());
        REQUIRE(loaded.has_value());
        CHECK(loaded->layers[0].hexContrast == doctest::Approx(terrain::MAX_HEX_TILING_CONTRAST));
        CHECK(loaded->layers[0].hexCellScale == doctest::Approx(terrain::MIN_HEX_TILING_CELL_SCALE));
        CHECK(loaded->antiTiling.macroVariationStrength
              == doctest::Approx(terrain::MACRO_VARIATION_MAX_STRENGTH));
        CHECK(loaded->antiTiling.macroVariationSize0
              == doctest::Approx(terrain::MACRO_VARIATION_MIN_SIZE));
        CHECK(loaded->antiTiling.distanceRescaleWidth
              == doctest::Approx(terrain::DISTANCE_RESCALE_MIN_WIDTH));
        CHECK(loaded->antiTiling.distanceRescaleScale
              == doctest::Approx(terrain::DISTANCE_RESCALE_MAX_SCALE));

        fs::remove(path);
    }

    TEST_CASE("a layer with no material source round-trips as invalid")
    {
        terrain::TerrainMaterialData mat;
        mat.activeLayerCount = 1;
        mat.layers[0].name = "Empty";
        // materialRef left default (invalid)

        const auto path = tempPath("vf_test_terrainmat_invalidref.vfterrainmat");
        REQUIRE(terrain::TerrainMaterialAsset::save(path.string(), mat));

        auto loaded = terrain::TerrainMaterialAsset::load(path.string());
        REQUIRE(loaded.has_value());
        CHECK_FALSE(loaded->layers[0].materialRef.isValid());

        fs::remove(path);
    }

    TEST_CASE("legacy .vfterrainmat (inline PBR, no materialRef) still loads")
    {
        // A pre-VK-1486 file: inline texture refs + PBR scalars, no materialRef key. The removed
        // inline PBR is ignored; terrain-local fields (name/tiling/blend/enabled) still load.
        // VK-1609: "Overlay" was retired and now loads as Linear (it always rendered as a plain
        // linear average anyway), and the absent heightContrast key takes its default.
        const auto path = tempPath("vf_test_terrainmat_legacy.vfterrainmat");
        {
            std::ofstream f(path);
            f << R"JSON({
  "version": "1.0",
  "uuid": "legacy-uuid",
  "name": "Legacy",
  "activeLayerCount": 1,
  "layers": [
    {
      "name": "OldLayer",
      "albedoTextureRef": "0011223344556677",
      "ormTextureRef": "8899aabbccddeeff",
      "tilingScale": 3.0,
      "roughness": 0.4,
      "metallic": 0.2,
      "ao": 0.9,
      "emissionStrength": 1.5,
      "blendMode": "Overlay",
      "enabled": true
    }
  ]
})JSON";
        }

        auto loaded = terrain::TerrainMaterialAsset::load(path.string());
        REQUIRE(loaded.has_value());
        CHECK(loaded->name == "Legacy");
        CHECK(loaded->layers[0].name == "OldLayer");
        CHECK(loaded->layers[0].tilingScale == doctest::Approx(3.0f));
        CHECK(loaded->layers[0].blendMode == terrain::TerrainLayerBlendMode::Linear);
        CHECK(loaded->layers[0].heightContrast == doctest::Approx(4.0f));
        CHECK(loaded->layers[0].enabled == true);
        CHECK_FALSE(loaded->layers[0].materialRef.isValid());
        // VK-1611/VK-1612: absent keys must read as OFF, so an old file cannot silently acquire a
        // new shader permutation (and the cost that comes with it) just by being loaded.
        CHECK(loaded->layers[0].hexTiling == false);
        CHECK(loaded->antiTiling.macroVariationStrength == doctest::Approx(0.0f));
        CHECK(loaded->antiTiling.distanceRescaleStrength == doctest::Approx(0.0f));

        fs::remove(path);
    }

    TEST_CASE("saved JSON carries materialRef and drops removed inline-PBR keys")
    {
        terrain::TerrainMaterialData mat;
        mat.activeLayerCount = 1;
        mat.layers[0].name = "L0";
        mat.layers[0].materialRef = makeRef();

        const auto path = tempPath("vf_test_terrainmat_jsonshape.vfterrainmat");
        REQUIRE(terrain::TerrainMaterialAsset::save(path.string(), mat));

        nlohmann::json j;
        {
            std::ifstream f(path);
            f >> j;
        }
        fs::remove(path);

        REQUIRE(j.contains("layers"));
        REQUIRE(j["layers"].is_array());
        const auto& layer0 = j["layers"][0];
        CHECK(layer0.contains("materialRef"));
        CHECK(layer0.contains("tilingScale"));
        // The inline PBR fields are gone from the format.
        CHECK_FALSE(layer0.contains("albedoTextureRef"));
        CHECK_FALSE(layer0.contains("normalTextureRef"));
        CHECK_FALSE(layer0.contains("ormTextureRef"));
        CHECK_FALSE(layer0.contains("roughness"));
        CHECK_FALSE(layer0.contains("metallic"));
        CHECK_FALSE(layer0.contains("emissionStrength"));
        // VK-1612 per-layer keys.
        CHECK(layer0.contains("hexTiling"));
        CHECK(layer0.contains("hexCellScale"));
        CHECK(layer0.contains("hexContrast"));
        CHECK(layer0.contains("hexRotation"));
        // VK-1614 per-layer keys.
        CHECK(layer0.contains("weatherResponse"));
        CHECK(layer0.contains("porosity"));
        CHECK(layer0.contains("snowRetention"));
        // VK-1611 material-global block.
        REQUIRE(j.contains("antiTiling"));
        CHECK(j["antiTiling"].contains("macroVariationStrength"));
        CHECK(j["antiTiling"].contains("distanceRescaleKnee"));
        // Additive only: the format version is unchanged, exactly as VK-1609 did it.
        CHECK(j["version"] == terrain::TERRAIN_MATERIAL_FORMAT_VERSION);
    }
}

TEST_SUITE("TerrainLayerPBRResolver")
{
    TEST_CASE("no material -> defaults, tilingScale from layer")
    {
        terrain::TerrainMaterialLayer layer;
        layer.tilingScale = 7.0f;

        const render::gpudriven::ResolvedTerrainLayerPBR r =
            render::gpudriven::resolveTerrainLayerPBR(layer, nullptr);

        CHECK(r.albedoPath.empty());
        CHECK(r.normalPath.empty());
        CHECK(r.ormPath.empty());
        CHECK(r.emissionPath.empty());
        CHECK(r.tilingScale == doctest::Approx(7.0f));
        // struct defaults when no material is assigned
        CHECK(r.roughness == doctest::Approx(0.9f));
        CHECK(r.metallic == doctest::Approx(0.0f));
        CHECK(r.ao == doctest::Approx(1.0f));
        CHECK(r.emissionStrength == doctest::Approx(0.0f));
    }

    TEST_CASE("material supplies textures + scalars; tilingScale stays terrain-local")
    {
        terrain::TerrainMaterialLayer layer;
        layer.tilingScale = 2.0f;

        render::mesh::ExtractedPBRValues pbr;
        pbr.albedoTexturePath = "A.vfImage";
        pbr.normalTexturePath = "N.vfImage";
        pbr.ormTexturePath = "ORM.vfImage";
        pbr.emissionTexturePath = "E.vfImage";
        pbr.roughness = 0.3f;
        pbr.metallic = 0.6f;
        pbr.ao = 0.7f;
        pbr.emission = 2.5f;

        const render::gpudriven::ResolvedTerrainLayerPBR r =
            render::gpudriven::resolveTerrainLayerPBR(layer, &pbr);

        CHECK(r.albedoPath == "A.vfImage");
        CHECK(r.normalPath == "N.vfImage");
        CHECK(r.ormPath == "ORM.vfImage");
        CHECK(r.emissionPath == "E.vfImage");
        CHECK(r.roughness == doctest::Approx(0.3f));
        CHECK(r.metallic == doctest::Approx(0.6f));
        CHECK(r.ao == doctest::Approx(0.7f));
        CHECK(r.emissionStrength == doctest::Approx(2.5f));
        CHECK(r.tilingScale == doctest::Approx(2.0f)); // from the layer, never the material
    }

    TEST_CASE("material without textures -> empty paths, scalars still from material")
    {
        terrain::TerrainMaterialLayer layer;
        layer.tilingScale = 1.0f;

        render::mesh::ExtractedPBRValues pbr; // no texture paths set
        pbr.roughness = 0.25f;
        pbr.metallic = 0.1f;

        const render::gpudriven::ResolvedTerrainLayerPBR r =
            render::gpudriven::resolveTerrainLayerPBR(layer, &pbr);

        CHECK(r.albedoPath.empty());
        CHECK(r.normalPath.empty());
        CHECK(r.ormPath.empty());
        CHECK(r.emissionPath.empty());
        CHECK(r.roughness == doctest::Approx(0.25f));
        CHECK(r.metallic == doctest::Approx(0.1f));
    }
}
