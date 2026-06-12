#include <doctest.h>
#include <resource/AssetTypes.hpp>
#include <asset/AssetMetadata.hpp>
#include <asset/AssetMetadataSerializer.hpp>
#include <asset/AssetDatabaseMigrator.hpp>
#include <asset/AssetGUID.hpp>
#include <filesystem>

// Prefab (and Theme) as first-class asset types: extension detection,
// type-string round-trip, and .vfmeta sidecar persistence.

TEST_SUITE("AssetPrefabType")
{
    TEST_CASE("extension detection maps .vfprefab and .vftheme")
    {
        CHECK(asset::AssetDatabaseMigrator::detectAssetType(".vfprefab") == resource::AssetType::Prefab);
        CHECK(asset::AssetDatabaseMigrator::detectAssetType(".vfPrefab") == resource::AssetType::Prefab);
        CHECK(asset::AssetDatabaseMigrator::detectAssetType(".vftheme") == resource::AssetType::Theme);
        CHECK(asset::AssetDatabaseMigrator::detectAssetTypeFromPath("ui/CommandCard.vfPrefab")
              == resource::AssetType::Prefab);
    }

    TEST_CASE("type name round-trips through the metadata serializer")
    {
        CHECK(resource::assetTypeName(resource::AssetType::Prefab) == std::string("Prefab"));
        CHECK(asset::AssetMetadataSerializer::stringToAssetType("Prefab") == resource::AssetType::Prefab);
        CHECK(resource::assetTypeName(resource::AssetType::Theme) == std::string("Theme"));
        CHECK(asset::AssetMetadataSerializer::stringToAssetType("Theme") == resource::AssetType::Theme);
    }

    TEST_CASE(".vfmeta sidecar persists a prefab GUID + type")
    {
        namespace fs = std::filesystem;
        fs::path dir = fs::temp_directory_path() / "vf_prefab_meta_test";
        fs::create_directories(dir);
        fs::path metaPath = dir / "Widget.vfPrefab.vfmeta";

        asset::AssetMetadata metadata;
        metadata.guid = asset::AssetGUID::generate();
        metadata.type = resource::AssetType::Prefab;
        metadata.importSourcePath = "Widget.vfPrefab";
        metadata.formatVersion = 1;

        REQUIRE(asset::AssetMetadataSerializer::save(metadata, metaPath));

        auto loaded = asset::AssetMetadataSerializer::load(metaPath);
        REQUIRE(loaded.has_value());
        CHECK(loaded->guid == metadata.guid);
        CHECK(loaded->type == resource::AssetType::Prefab);
        CHECK(loaded->importSourcePath == "Widget.vfPrefab");

        fs::remove_all(dir);
    }
}
