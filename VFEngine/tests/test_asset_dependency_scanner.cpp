#include <doctest.h>
#include <asset/AssetDatabase.hpp>
#include <asset/AssetExtensions.hpp>
#include <asset/AssetGUID.hpp>
#include <asset/AssetMetadata.hpp>
#include <asset/AssetMetadataSerializer.hpp>
#include <asset/DependencyScanner.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// ============================================================
// Asset reference expansion: GUID-aware DependencyScanner,
// .vfmeta formatVersion 2 dependencies, and graph seeding from
// meta files in rebuildFromMetaFiles.
//
// AssetDatabase is a process-wide singleton, so each case
// clears it and builds its own temp project tree.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    struct TempProject
    {
        fs::path root;

        TempProject(const std::string& name)
        {
            root = fs::temp_directory_path() / name;
            std::error_code ec;
            fs::remove_all(root, ec);
            fs::create_directories(root, ec);
            asset::AssetDatabase::instance().clear();
        }

        ~TempProject()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
            asset::AssetDatabase::instance().clear();
        }

        fs::path writeFile(const std::string& relPath, const std::string& content) const
        {
            fs::path p = root / relPath;
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            std::ofstream file(p);
            file << content;
            return p;
        }
    };

    asset::AssetGUID registerAsset(const fs::path& path, resource::AssetType type)
    {
        return asset::AssetDatabase::instance().registerAsset(path.string(), type);
    }

    bool contains(const std::vector<asset::AssetGUID>& guids, const asset::AssetGUID& guid)
    {
        return std::find(guids.begin(), guids.end(), guid) != guids.end();
    }
}

TEST_SUITE("AssetDependencyScanner")
{

TEST_CASE("isStrictHex16 accepts only the exact toString form")
{
    CHECK(asset::AssetGUID::isStrictHex16("0123456789abcdef"));
    CHECK(asset::AssetGUID::isStrictHex16("00000000DEADBEEF"));

    CHECK_FALSE(asset::AssetGUID::isStrictHex16(""));
    CHECK_FALSE(asset::AssetGUID::isStrictHex16("0123456789abcde"));    // 15 chars
    CHECK_FALSE(asset::AssetGUID::isStrictHex16("0123456789abcdef0")); // 17 chars
    CHECK_FALSE(asset::AssetGUID::isStrictHex16("0123456789abcdeg"));  // non-hex
    CHECK_FALSE(asset::AssetGUID::isStrictHex16("assets/foo.vfmat"));  // stoull would parse "a"!
}

TEST_CASE("stringToAssetType round-trips every enum value")
{
    using resource::AssetType;
    for (uint8_t i = 0; i < static_cast<uint8_t>(AssetType::COUNT); ++i)
    {
        auto type = static_cast<AssetType>(i);
        CAPTURE(resource::assetTypeName(type));
        CHECK(asset::AssetMetadataSerializer::stringToAssetType(resource::assetTypeName(type)) == type);
    }

    // Regression: these two were missing from the hand-written if-chain
    CHECK(asset::AssetMetadataSerializer::stringToAssetType("TerrainMaterial")
          == AssetType::TerrainMaterial);
    CHECK(asset::AssetMetadataSerializer::stringToAssetType("BehaviorTree")
          == AssetType::BehaviorTree);
    CHECK(asset::AssetMetadataSerializer::stringToAssetType("NotAType") == AssetType::COUNT);
}

TEST_CASE("path-based reference produces forward and reverse edges")
{
    TempProject proj("vf_depscan_path_rule");
    auto& db = asset::AssetDatabase::instance();
    REQUIRE(db.rebuildFromMetaFiles(proj.root.string()));

    auto texPath = proj.writeFile("textures/wood.vfimage", "binary");
    auto texGuid = registerAsset(texPath, resource::AssetType::Texture);

    std::string texPathFwd = texPath.string();
    std::replace(texPathFwd.begin(), texPathFwd.end(), '\\', '/');
    auto matPath = proj.writeFile("materials/wood.vfmat",
        "{ \"albedoTextureRefPath\": \"" + texPathFwd + "\" }");
    auto matGuid = registerAsset(matPath, resource::AssetType::Material);

    auto deps = asset::DependencyScanner::scanAsset(matGuid, matPath.string(), proj.root.string());

    CHECK(contains(deps, texGuid));
    CHECK(contains(db.getDependencies(matGuid), texGuid));
    CHECK(contains(db.getDependents(texGuid), matGuid));
}

TEST_CASE("GUID-only reference with no path fallback is found")
{
    TempProject proj("vf_depscan_guid_rule");
    auto& db = asset::AssetDatabase::instance();
    REQUIRE(db.rebuildFromMetaFiles(proj.root.string()));

    auto fontPath = proj.writeFile("fonts/main.vffont", "binary");
    auto fontGuid = registerAsset(fontPath, resource::AssetType::Font);

    auto prefabPath = proj.writeFile("ui/hud.vfprefab",
        "{ \"fontRef\": \"" + fontGuid.toString() + "\" }");
    auto prefabGuid = registerAsset(prefabPath, resource::AssetType::Prefab);

    auto deps = asset::DependencyScanner::scanAsset(prefabGuid, prefabPath.string(), proj.root.string());

    CHECK(contains(deps, fontGuid));
    CHECK(contains(db.getDependents(fontGuid), prefabGuid));
}

TEST_CASE("hex strings that do not resolve in the database are not dependencies")
{
    TempProject proj("vf_depscan_negative");
    auto& db = asset::AssetDatabase::instance();
    REQUIRE(db.rebuildFromMetaFiles(proj.root.string()));

    auto matPath = proj.writeFile("materials/odd.vfmat",
        "{ \"someHash\": \"00000000deadbeef\", \"name\": \"odd\" }");
    auto matGuid = registerAsset(matPath, resource::AssetType::Material);

    auto deps = asset::DependencyScanner::scanAsset(matGuid, matPath.string(), proj.root.string());
    CHECK(deps.empty());
}

TEST_CASE("project-relative path reference resolves against the project root")
{
    TempProject proj("vf_depscan_relative");
    auto& db = asset::AssetDatabase::instance();
    REQUIRE(db.rebuildFromMetaFiles(proj.root.string()));

    auto texPath = proj.writeFile("textures/grass.vfimage", "binary");
    auto texGuid = registerAsset(texPath, resource::AssetType::Texture);

    auto matPath = proj.writeFile("materials/grass.vfmat",
        "{ \"albedoTextureRefPath\": \"textures/grass.vfimage\" }");
    auto matGuid = registerAsset(matPath, resource::AssetType::Material);

    auto deps = asset::DependencyScanner::scanAsset(matGuid, matPath.string(), proj.root.string());
    CHECK(contains(deps, texGuid));
}

TEST_CASE("theme and behavior tree containers are scanned")
{
    TempProject proj("vf_depscan_containers");
    auto& db = asset::AssetDatabase::instance();
    REQUIRE(db.rebuildFromMetaFiles(proj.root.string()));

    auto fontPath = proj.writeFile("fonts/ui.vffont", "binary");
    auto fontGuid = registerAsset(fontPath, resource::AssetType::Font);

    auto themePath = proj.writeFile("ui/dark.vftheme",
        "{ \"fontRef\": \"" + fontGuid.toString() + "\" }");
    auto themeGuid = registerAsset(themePath, resource::AssetType::Theme);

    auto texPath = proj.writeFile("textures/icon.vfimage", "binary");
    auto texGuid = registerAsset(texPath, resource::AssetType::Texture);

    auto btPath = proj.writeFile("ai/guard.vfbehaviortree",
        "{ \"iconRef\": \"" + texGuid.toString() + "\" }");
    auto btGuid = registerAsset(btPath, resource::AssetType::BehaviorTree);

    CHECK(contains(asset::DependencyScanner::scanAsset(themeGuid, themePath.string(), proj.root.string()),
                   fontGuid));
    CHECK(contains(asset::DependencyScanner::scanAsset(btGuid, btPath.string(), proj.root.string()),
                   texGuid));

    // Non-container binary assets are skipped entirely
    CHECK(asset::DependencyScanner::scanAsset(texGuid, texPath.string(), proj.root.string()).empty());
}

TEST_CASE("meta v2 dependencies round-trip sorted; v1 loads with empty deps")
{
    TempProject proj("vf_meta_v2_roundtrip");

    SUBCASE("v2 save/load round-trip")
    {
        asset::AssetMetadata meta;
        meta.guid = asset::AssetGUID::generate();
        meta.type = resource::AssetType::Material;
        meta.dependencies = {
            asset::AssetGUID::fromValue(0xBBBBBBBBBBBBBBBBull),
            asset::AssetGUID::fromValue(0xAAAAAAAAAAAAAAAAull)
        };

        fs::path metaPath = proj.root / "asset.vfmat.vfmeta";
        REQUIRE(asset::AssetMetadataSerializer::save(meta, metaPath));

        auto loaded = asset::AssetMetadataSerializer::load(metaPath);
        REQUIRE(loaded.has_value());
        CHECK(loaded->guid == meta.guid);
        CHECK(loaded->formatVersion == asset::AssetMetadata::kCurrentFormatVersion);
        REQUIRE(loaded->dependencies.size() == 2);
        // Written sorted regardless of in-memory order
        CHECK(loaded->dependencies[0] == asset::AssetGUID::fromValue(0xAAAAAAAAAAAAAAAAull));
        CHECK(loaded->dependencies[1] == asset::AssetGUID::fromValue(0xBBBBBBBBBBBBBBBBull));
    }

    SUBCASE("hand-written v1 meta loads unchanged with empty deps")
    {
        fs::path metaPath = proj.writeFile("legacy.vfimage.vfmeta",
            "{ \"formatVersion\": 1, \"guid\": \"0123456789abcdef\","
            " \"type\": \"Texture\", \"importSource\": \"src.png\", \"importTimestamp\": \"\" }");

        auto loaded = asset::AssetMetadataSerializer::load(metaPath);
        REQUIRE(loaded.has_value());
        CHECK(loaded->formatVersion == 1);
        CHECK(loaded->dependencies.empty());
        CHECK(loaded->type == resource::AssetType::Texture);
        CHECK(loaded->importSourcePath == "src.png");
    }

    SUBCASE("invalid dependency entries are skipped on load")
    {
        fs::path metaPath = proj.writeFile("odd.vfmat.vfmeta",
            "{ \"formatVersion\": 2, \"guid\": \"0123456789abcdef\", \"type\": \"Material\","
            " \"dependencies\": [\"not-a-guid\", \"00000000deadbeef\", 42] }");

        auto loaded = asset::AssetMetadataSerializer::load(metaPath);
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->dependencies.size() == 1);
        CHECK(loaded->dependencies[0] == asset::AssetGUID::fromValue(0xDEADBEEFull));
    }
}

TEST_CASE("scanAsset writes dependencies into the .vfmeta sidecar")
{
    TempProject proj("vf_depscan_meta_write");
    auto& db = asset::AssetDatabase::instance();
    REQUIRE(db.rebuildFromMetaFiles(proj.root.string()));

    auto texPath = proj.writeFile("textures/rock.vfimage", "binary");
    auto texGuid = registerAsset(texPath, resource::AssetType::Texture);

    auto matPath = proj.writeFile("materials/rock.vfmat",
        "{ \"albedoTextureRef\": \"" + texGuid.toString() + "\" }");
    auto matGuid = registerAsset(matPath, resource::AssetType::Material);

    // Sidecar must exist for the scanner to update it
    asset::AssetMetadata matMeta;
    matMeta.guid = matGuid;
    matMeta.type = resource::AssetType::Material;
    auto metaPath = asset::AssetMetadataSerializer::getMetaPath(matPath);
    REQUIRE(asset::AssetMetadataSerializer::save(matMeta, metaPath));

    asset::DependencyScanner::scanAsset(matGuid, matPath.string(), proj.root.string());

    auto loaded = asset::AssetMetadataSerializer::load(metaPath);
    REQUIRE(loaded.has_value());
    CHECK(loaded->formatVersion == asset::AssetMetadata::kCurrentFormatVersion);
    REQUIRE(loaded->dependencies.size() == 1);
    CHECK(loaded->dependencies[0] == texGuid);
    CHECK(loaded->guid == matGuid);
    CHECK(loaded->type == resource::AssetType::Material);

    // Unchanged deps must not rewrite the file (VCS churn guard)
    auto firstWrite = fs::last_write_time(metaPath);
    asset::DependencyScanner::scanAsset(matGuid, matPath.string(), proj.root.string());
    CHECK(fs::last_write_time(metaPath) == firstWrite);
}

TEST_CASE("rebuildFromMetaFiles seeds the dependency graph from v2 metas")
{
    TempProject proj("vf_rebuild_seeds_deps");
    auto& db = asset::AssetDatabase::instance();

    asset::AssetGUID texGuid = asset::AssetGUID::generate();
    asset::AssetGUID matGuid = asset::AssetGUID::generate();

    proj.writeFile("textures/brick.vfimage", "binary");
    asset::AssetMetadata texMeta;
    texMeta.guid = texGuid;
    texMeta.type = resource::AssetType::Texture;
    REQUIRE(asset::AssetMetadataSerializer::save(
        texMeta, proj.root / "textures/brick.vfimage.vfmeta"));

    proj.writeFile("materials/brick.vfmat", "{}");
    asset::AssetMetadata matMeta;
    matMeta.guid = matGuid;
    matMeta.type = resource::AssetType::Material;
    matMeta.dependencies = {texGuid, asset::AssetGUID::fromValue(0x1111111111111111ull)};
    REQUIRE(asset::AssetMetadataSerializer::save(
        matMeta, proj.root / "materials/brick.vfmat.vfmeta"));

    // No content scan — graph must come from the metas alone
    REQUIRE(db.rebuildFromMetaFiles(proj.root.string()));

    auto deps = db.getDependencies(matGuid);
    CHECK(contains(deps, texGuid));
    // Edge to the unregistered GUID is dropped
    CHECK_FALSE(contains(deps, asset::AssetGUID::fromValue(0x1111111111111111ull)));
    CHECK(contains(db.getDependents(texGuid), matGuid));
}

TEST_CASE("extension registry covers new container types and stays consistent")
{
    using asset::extensions::isJsonContainerExtension;
    using asset::extensions::isAssetExtension;
    using asset::extensions::typeForExtension;

    CHECK(isJsonContainerExtension(".vftheme"));
    CHECK(isJsonContainerExtension(".vfbehaviortree"));
    CHECK(isJsonContainerExtension(".vfsettings"));
    CHECK(isJsonContainerExtension(".vfinputmapping"));
    CHECK(isJsonContainerExtension(".VFScene"));   // case-insensitive
    CHECK_FALSE(isJsonContainerExtension(".vfmesh"));
    CHECK_FALSE(isJsonContainerExtension(".vfimage"));

    // Every container is also a known asset extension
    for (const auto& ext : asset::extensions::jsonContainerExtensions())
    {
        CAPTURE(ext);
        CHECK(isAssetExtension(ext));
    }

    CHECK(typeForExtension(".vftheme") == resource::AssetType::Theme);
    CHECK(typeForExtension(".vfprefab") == resource::AssetType::Prefab);
    CHECK(typeForExtension(".vfterrainmat") == resource::AssetType::TerrainMaterial);
    CHECK(typeForExtension(".unknown") == resource::AssetType::COUNT);
}

}
