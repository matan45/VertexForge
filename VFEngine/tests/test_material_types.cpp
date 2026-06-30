#include <doctest.h>
#include <material/MaterialInstanceTypes.hpp>
#include <material/MaterialTypes.hpp>
#include <asset/AssetGUID.hpp>
#include <asset/AssetRef.hpp>

// ============================================================
// VK-1060: Material types unit tests
// ============================================================

TEST_SUITE("MaterialTypes") {

// ---- MaterialInstanceData ----

TEST_CASE("MaterialInstanceData: default has no overrides") {
    material::MaterialInstanceData data;
    CHECK_FALSE(data.hasOverrides());
}

TEST_CASE("MaterialInstanceData: setTextureOverride makes isTextureOverridden true") {
    material::MaterialInstanceData data;
    auto guid = asset::AssetGUID::generate();
    auto ref = asset::AssetRef::fromGUID(guid);

    data.setTextureOverride(material::TextureSlot::Albedo, ref);

    CHECK(data.isTextureOverridden(material::TextureSlot::Albedo));
    CHECK_FALSE(data.isTextureOverridden(material::TextureSlot::Normal));
}

TEST_CASE("MaterialInstanceData: getTextureOverride returns set ref") {
    material::MaterialInstanceData data;
    auto guid = asset::AssetGUID::generate();
    auto ref = asset::AssetRef::fromGUID(guid);

    data.setTextureOverride(material::TextureSlot::Albedo, ref);

    auto result = data.getTextureOverride(material::TextureSlot::Albedo);
    CHECK(result.isValid());
    CHECK(result.getGUID() == guid);
}

TEST_CASE("MaterialInstanceData: getTextureOverride returns invalid for unset slot") {
    material::MaterialInstanceData data;
    auto result = data.getTextureOverride(material::TextureSlot::Normal);
    CHECK_FALSE(result.isValid());
}

TEST_CASE("MaterialInstanceData: setTextureOverride with invalid ref removes override") {
    material::MaterialInstanceData data;
    auto guid = asset::AssetGUID::generate();
    auto ref = asset::AssetRef::fromGUID(guid);

    data.setTextureOverride(material::TextureSlot::Albedo, ref);
    CHECK(data.isTextureOverridden(material::TextureSlot::Albedo));

    data.setTextureOverride(material::TextureSlot::Albedo, asset::AssetRef::invalid());
    CHECK_FALSE(data.isTextureOverridden(material::TextureSlot::Albedo));
}

TEST_CASE("MaterialInstanceData: hasOverrides with scalar override") {
    material::MaterialInstanceData data;
    CHECK_FALSE(data.hasOverrides());
    data.metallicOverride = 0.5f;
    CHECK(data.hasOverrides());
}

TEST_CASE("MaterialInstanceData: clearAllOverrides clears everything") {
    material::MaterialInstanceData data;
    auto guid = asset::AssetGUID::generate();
    auto ref = asset::AssetRef::fromGUID(guid);

    data.setTextureOverride(material::TextureSlot::Albedo, ref);
    data.metallicOverride = 0.8f;
    data.roughnessOverride = 0.3f;
    data.albedoOverride = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    CHECK(data.hasOverrides());

    data.clearAllOverrides();
    CHECK_FALSE(data.hasOverrides());
    CHECK_FALSE(data.isTextureOverridden(material::TextureSlot::Albedo));
    CHECK_FALSE(data.metallicOverride.has_value());
    CHECK_FALSE(data.roughnessOverride.has_value());
    CHECK_FALSE(data.albedoOverride.has_value());
}

// ---- isInstanceFile / isMaterialFile ----

TEST_CASE("isInstanceFile: true for .vfMatInstance extension") {
    CHECK(material::isInstanceFile("assets/wood.vfMatInstance"));
    CHECK(material::isInstanceFile("assets/wood.vfmatinstance"));
    CHECK(material::isInstanceFile(".vfMatInstance"));
}

TEST_CASE("isInstanceFile: false for .vfMat extension") {
    CHECK_FALSE(material::isInstanceFile("assets/wood.vfMat"));
}

TEST_CASE("isMaterialFile: true for .vfMat extension") {
    CHECK(material::isMaterialFile("assets/wood.vfMat"));
    CHECK(material::isMaterialFile("assets/wood.vfmat"));
    CHECK(material::isMaterialFile("assets/wood.vfMaterial"));
    CHECK(material::isMaterialFile(".vfMat"));
}

TEST_CASE("isMaterialFile: false for .vfMatInstance extension") {
    CHECK_FALSE(material::isMaterialFile("assets/wood.vfMatInstance"));
}

// ---- pinTypeToString ----

TEST_CASE("pinTypeToString: all PinType enum values return non-empty string") {
    CHECK_FALSE(material::pinTypeToString(material::PinType::Float).empty());
    CHECK_FALSE(material::pinTypeToString(material::PinType::Vec2).empty());
    CHECK_FALSE(material::pinTypeToString(material::PinType::Vec3).empty());
    CHECK_FALSE(material::pinTypeToString(material::PinType::Vec4).empty());
    CHECK_FALSE(material::pinTypeToString(material::PinType::Texture2D).empty());

    CHECK(material::pinTypeToString(material::PinType::Float) == "Float");
    CHECK(material::pinTypeToString(material::PinType::Vec2) == "Vec2");
    CHECK(material::pinTypeToString(material::PinType::Vec3) == "Vec3");
    CHECK(material::pinTypeToString(material::PinType::Vec4) == "Vec4");
    CHECK(material::pinTypeToString(material::PinType::Texture2D) == "Texture2D");
}

// ---- TextureSlot::Count ----

TEST_CASE("TextureSlot::Count equals 16") {
    CHECK(static_cast<int>(material::TextureSlot::Count) == 16);
}

} // TEST_SUITE
