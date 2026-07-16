#include <doctest.h>

#include <material/PipelineWarmupList.hpp>
#include <material/PipelineWarmupManifest.hpp>
#include <components/CoreComponents.hpp>
#include <asset/AssetDatabase.hpp>
#include <asset/AssetRef.hpp>
#include <resource/AssetTypes.hpp>

#include <entt/entt.hpp>
#include <algorithm>
#include <string>
#include <vector>

// ============================================================
// VK-1532: async pipeline warm-up — CPU-testable pieces.
// The pipeline creation itself needs a Vulkan device + shaderc
// and is verified manually; here we cover the pure enumeration,
// dedupe, and PSO-manifest serialization logic.
// ============================================================

namespace
{
    bool contains(const std::vector<std::string>& v, const std::string& s)
    {
        return std::find(v.begin(), v.end(), s) != v.end();
    }
}

TEST_SUITE("PipelineWarmup")
{

TEST_CASE("dedupeWarmupPaths drops empties, sorts, and de-duplicates")
{
    const std::vector<std::string> raw = { "b", "", "a", "b", "c", "a", "" };
    CHECK(material::dedupeWarmupPaths(raw) == std::vector<std::string>{ "a", "b", "c" });
}

TEST_CASE("dedupeWarmupPaths handles empty / all-empty input")
{
    CHECK(material::dedupeWarmupPaths({}).empty());
    CHECK(material::dedupeWarmupPaths({ "", "" }).empty());
}

TEST_CASE("collectMaterialPathsForWarmup unions default + submesh materials, deduped")
{
    auto& db = asset::AssetDatabase::instance();
    db.clear();

    const asset::AssetGUID gRed   = db.registerAsset("materials/red.vfMat",   resource::AssetType::Material);
    const asset::AssetGUID gBlue  = db.registerAsset("materials/blue.vfMat",  resource::AssetType::Material);
    const asset::AssetGUID gGreen = db.registerAsset("materials/green.vfMat", resource::AssetType::Material);

    const std::string pRed   = asset::AssetRef::fromGUID(gRed).resolve();
    const std::string pBlue  = asset::AssetRef::fromGUID(gBlue).resolve();
    const std::string pGreen = asset::AssetRef::fromGUID(gGreen).resolve();
    REQUIRE_FALSE(pRed.empty());

    entt::registry registry;

    // Entity 1: default red, submesh blue.
    {
        const auto e = registry.create();
        auto& mat = registry.emplace<components::MaterialComponent>(e);
        mat.defaultMaterialRef = asset::AssetRef::fromGUID(gRed);
        mat.subMeshMaterials["body"] = asset::AssetRef::fromGUID(gBlue);
    }
    // Entity 2: default green, submesh red (overlaps entity 1's red).
    {
        const auto e = registry.create();
        auto& mat = registry.emplace<components::MaterialComponent>(e);
        mat.defaultMaterialRef = asset::AssetRef::fromGUID(gGreen);
        mat.subMeshMaterials["a"] = asset::AssetRef::fromGUID(gRed);
    }
    // Entity 3: invalid default, only a submesh (blue again).
    {
        const auto e = registry.create();
        auto& mat = registry.emplace<components::MaterialComponent>(e);
        mat.subMeshMaterials["a"] = asset::AssetRef::fromGUID(gBlue);
    }
    // Entity 4: a MaterialComponent with all-invalid refs contributes nothing.
    {
        const auto e = registry.create();
        registry.emplace<components::MaterialComponent>(e);
    }

    const auto result = material::collectMaterialPathsForWarmup(registry);

    CHECK(result.size() == 3);
    CHECK(contains(result, pRed));
    CHECK(contains(result, pBlue));
    CHECK(contains(result, pGreen));
    CHECK(std::is_sorted(result.begin(), result.end())); // deterministic order

    db.clear();
}

TEST_CASE("collectMaterialPathsForWarmup returns empty for a registry with no materials")
{
    entt::registry registry;
    registry.create();
    registry.create();
    CHECK(material::collectMaterialPathsForWarmup(registry).empty());
}

TEST_CASE("PipelineWarmupManifest round-trips through JSON")
{
    material::PipelineWarmupManifest manifest;
    manifest.scenes["sceneGuidA"] = { "matGuid1", "matGuid2" };
    manifest.scenes["sceneGuidB"] = { "matGuid3" };

    const std::string json = manifest.toJson();
    const material::PipelineWarmupManifest parsed = material::PipelineWarmupManifest::fromJson(json);

    REQUIRE(parsed.scenes.size() == 2);
    CHECK(parsed.scenes.at("sceneGuidA") == std::vector<std::string>{ "matGuid1", "matGuid2" });
    CHECK(parsed.scenes.at("sceneGuidB") == std::vector<std::string>{ "matGuid3" });
}

TEST_CASE("PipelineWarmupManifest tolerates malformed / empty JSON")
{
    CHECK(material::PipelineWarmupManifest::fromJson("").scenes.empty());
    CHECK(material::PipelineWarmupManifest::fromJson("not json at all").scenes.empty());
    CHECK(material::PipelineWarmupManifest::fromJson("{}").scenes.empty());
    CHECK(material::PipelineWarmupManifest::fromJson(R"({"scenes":123})").scenes.empty());
}

}
