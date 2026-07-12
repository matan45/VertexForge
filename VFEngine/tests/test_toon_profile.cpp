#include <doctest.h>
#include <material/ToonProfile.hpp>
#include <material/ToonProfileAsset.hpp>
#include <material/MaterialTypes.hpp>
#include <material/MaterialAsset.hpp>
#include <render/gpudriven/GPUDrivenTypes.hpp>
#include <render/material/MaterialPBRExtractor.hpp>
#include <filesystem>
#include <fstream>

// ============================================================
// VK-1493 toon shading — CPU-only unit tests (doctest).
// ============================================================

TEST_CASE("ToonProfile asset JSON round-trips") {
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "vf_test_toon_roundtrip.vfToonProfile";

    material::ToonProfile p;
    p.shadeColor = glm::vec3(0.1f, 0.2f, 0.3f);
    p.midColor = glm::vec3(0.4f, 0.5f, 0.6f);
    p.shadowThreshold = 0.3f;
    p.midThreshold = 0.7f;
    p.bandSmoothness = 0.05f;
    p.giScale = 1.5f;
    p.specColor = glm::vec3(0.9f, 0.8f, 0.7f);
    p.specThreshold = 0.6f;
    p.specSmoothness = 0.04f;
    p.specIntensity = 2.0f;
    p.specShininess = 48.0f;
    p.rimColor = glm::vec3(0.2f, 0.9f, 0.4f);
    p.rimPower = 4.0f;
    p.rimIntensity = 1.25f;

    REQUIRE(material::ToonProfileAsset::save(tmp.string(), p));
    auto loaded = material::ToonProfileAsset::load(tmp.string());
    REQUIRE(loaded.has_value());

    CHECK(loaded->shadeColor.x == doctest::Approx(0.1f));
    CHECK(loaded->midColor.z == doctest::Approx(0.6f));
    CHECK(loaded->shadowThreshold == doctest::Approx(0.3f));
    CHECK(loaded->midThreshold == doctest::Approx(0.7f));
    CHECK(loaded->bandSmoothness == doctest::Approx(0.05f));
    CHECK(loaded->giScale == doctest::Approx(1.5f));
    CHECK(loaded->specColor.x == doctest::Approx(0.9f));
    CHECK(loaded->specThreshold == doctest::Approx(0.6f));
    CHECK(loaded->specIntensity == doctest::Approx(2.0f));
    CHECK(loaded->specShininess == doctest::Approx(48.0f));
    CHECK(loaded->rimColor.y == doctest::Approx(0.9f));
    CHECK(loaded->rimPower == doctest::Approx(4.0f));
    CHECK(loaded->rimIntensity == doctest::Approx(1.25f));
    CHECK(*loaded == p);

    fs::remove(tmp);
}

TEST_CASE("ToonProfile load defaults missing fields") {
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "vf_test_toon_partial.vfToonProfile";
    {
        std::ofstream f(tmp);
        f << R"({ "version": "1.0", "shadowThreshold": 0.42 })";
    }

    auto loaded = material::ToonProfileAsset::load(tmp.string());
    REQUIRE(loaded.has_value());

    material::ToonProfile defaults;
    CHECK(loaded->shadowThreshold == doctest::Approx(0.42f));           // present key
    CHECK(loaded->midThreshold == doctest::Approx(defaults.midThreshold)); // missing -> default
    CHECK(loaded->giScale == doctest::Approx(defaults.giScale));
    CHECK(loaded->rimIntensity == doctest::Approx(defaults.rimIntensity));
    CHECK(loaded->shadeColor == defaults.shadeColor);

    fs::remove(tmp);
}

TEST_CASE("ShadingModel enum string round-trips including Toon") {
    using material::ShadingModel;
    CHECK(material::stringToShadingModel(material::shadingModelToString(ShadingModel::DefaultLit)) == ShadingModel::DefaultLit);
    CHECK(material::stringToShadingModel(material::shadingModelToString(ShadingModel::Unlit)) == ShadingModel::Unlit);
    CHECK(material::stringToShadingModel(material::shadingModelToString(ShadingModel::Toon)) == ShadingModel::Toon);
    CHECK(material::shadingModelToString(ShadingModel::Toon) == "toon");
    // Unknown string falls back to DefaultLit.
    CHECK(material::stringToShadingModel("bogus") == ShadingModel::DefaultLit);
}

TEST_CASE("toon flag packing round-trips and does not collide with other object flags") {
    using namespace render::gpudriven;

    // Start from a flags word that already carries unrelated bits (all below bit 23).
    uint32_t flags = 0;
    flags |= ObjectFlags::ShadowStatic;   // bit 17
    flags |= ObjectFlags::Instanced;      // bit 15
    flags |= ObjectFlags::AdditiveBlend;  // bit 10
    flags |= (7u & ObjectFlags::LayerMask) << ObjectFlags::LayerShift;  // render layer 7 (bits 18-22)
    const uint32_t lowBitsBefore = flags & 0x007FFFFFu;               // bits 0..22

    ObjectFlags::packShadingFlags(flags, ObjectFlags::ShadingModelToon, 127);

    uint32_t model = (flags >> ObjectFlags::ShadingModelShift) & ObjectFlags::ShadingModelMask;
    uint32_t index = (flags >> ObjectFlags::ProfileIndexShift) & ObjectFlags::ProfileIndexMask;
    CHECK(model == ObjectFlags::ShadingModelToon);
    CHECK(index == 127u);

    // Packing the toon bits (23..31) must not disturb any bit at 0..22.
    CHECK((flags & 0x007FFFFFu) == lowBitsBefore);
    CHECK((flags & ObjectFlags::ShadowStatic) != 0u);
    CHECK((flags & ObjectFlags::Instanced) != 0u);
    CHECK((flags & ObjectFlags::AdditiveBlend) != 0u);
    CHECK(((flags >> ObjectFlags::LayerShift) & ObjectFlags::LayerMask) == 7u);

    // DefaultLit + profile 0 packs to nothing (legacy content stays bit-identical).
    uint32_t zero = 0;
    ObjectFlags::packShadingFlags(zero, ObjectFlags::ShadingModelDefaultLit, 0);
    CHECK(zero == 0u);
}

TEST_CASE("MaterialPBRExtractor propagates shading model + toon profile index") {
    // A default material with a PBR output node, marked Toon. No active GPU table in a
    // CPU test, so the resolved profile index falls back to 0 (the built-in default).
    material::MaterialData mat = material::MaterialAsset::createDefault("ToonMat");
    mat.shadingModel = material::ShadingModel::Toon;

    auto pbr = render::mesh::MaterialPBRExtractor::extractPBRFromMaterial(mat);
    CHECK(pbr.shadingModel == static_cast<uint8_t>(material::ShadingModel::Toon));
    CHECK(pbr.toonProfileIndex == 0);

    // A DefaultLit material reports shading model 0 (no toon).
    material::MaterialData lit = material::MaterialAsset::createDefault("LitMat");
    auto pbrLit = render::mesh::MaterialPBRExtractor::extractPBRFromMaterial(lit);
    CHECK(pbrLit.shadingModel == static_cast<uint8_t>(material::ShadingModel::DefaultLit));
}
