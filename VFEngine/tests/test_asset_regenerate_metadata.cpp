#include <doctest.h>
#include <asset/AssetDatabase.hpp>
#include <asset/AssetDatabaseMigrator.hpp>
#include <asset/AssetGUID.hpp>
#include <asset/AssetMetadata.hpp>
#include <asset/AssetMetadataSerializer.hpp>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    struct TempProject
    {
        fs::path root;

        explicit TempProject(const std::string& name)
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

    void saveSidecar(const fs::path& assetPath, const asset::AssetGUID& guid, resource::AssetType type)
    {
        asset::AssetMetadata metadata;
        metadata.guid = guid;
        metadata.type = type;
        REQUIRE(asset::AssetMetadataSerializer::save(
            metadata, asset::AssetMetadataSerializer::getMetaPath(assetPath)));
    }
}

TEST_SUITE("AssetRegenerateMetadata")
{

TEST_CASE("migrateProject regenerates only missing sidecars and ignores noise")
{
    TempProject proj("vf_regen_meta_missing");

    const fs::path missingPath = proj.writeFile("vfx/a.vfvfx", "{}");
    const fs::path existingPath = proj.writeFile("meshes/b.vfmesh", "mesh");
    proj.writeFile("notes.txt", "not an asset");
    proj.writeFile("assetdb.json", "{}");
    proj.writeFile("x.vfmeta", "{}");

    const asset::AssetGUID existingGuid = asset::AssetGUID::fromValue(0x0123456789ABCDEFull);
    saveSidecar(existingPath, existingGuid, resource::AssetType::Mesh);

    const auto result = asset::AssetDatabaseMigrator::migrateProject(proj.root.string());

    CHECK(result.assetsScanned == 2);
    CHECK(result.assetsRegistered == 1);
    CHECK(result.metaFilesCreated == 1);
    CHECK(result.errors.empty());

    const auto generated = asset::AssetMetadataSerializer::load(
        asset::AssetMetadataSerializer::getMetaPath(missingPath));
    REQUIRE(generated.has_value());
    CHECK(generated->type == resource::AssetType::VFX);
    CHECK(asset::AssetGUID::isStrictHex16(generated->guid.toString()));

    const auto preserved = asset::AssetMetadataSerializer::load(
        asset::AssetMetadataSerializer::getMetaPath(existingPath));
    REQUIRE(preserved.has_value());
    CHECK(preserved->guid == existingGuid);
    CHECK(preserved->type == resource::AssetType::Mesh);

    CHECK_FALSE(fs::exists(proj.root / "notes.txt.vfmeta"));
    CHECK_FALSE(fs::exists(proj.root / "assetdb.json.vfmeta"));
    CHECK_FALSE(fs::exists(proj.root / "x.vfmeta.vfmeta"));
}

TEST_CASE("migrateProject preserves known GUID when sidecar was deleted")
{
    TempProject proj("vf_regen_meta_known_guid");

    const fs::path assetPath = proj.writeFile("vfx/known.vfvfx", "{}");
    const asset::AssetGUID knownGuid = asset::AssetDatabase::instance().registerAsset(
        assetPath.string(), resource::AssetType::VFX);

    const auto result = asset::AssetDatabaseMigrator::migrateProject(proj.root.string());

    CHECK(result.assetsScanned == 1);
    CHECK(result.assetsRegistered == 1);
    CHECK(result.metaFilesCreated == 1);
    CHECK(result.errors.empty());

    const auto regenerated = asset::AssetMetadataSerializer::load(
        asset::AssetMetadataSerializer::getMetaPath(assetPath));
    REQUIRE(regenerated.has_value());
    CHECK(regenerated->guid == knownGuid);
    CHECK(regenerated->type == resource::AssetType::VFX);
}

TEST_CASE("migrateProject is idempotent after regenerating sidecars")
{
    TempProject proj("vf_regen_meta_idempotent");

    const fs::path assetPath = proj.writeFile("vfx/loop.vfvfx", "{}");
    const auto first = asset::AssetDatabaseMigrator::migrateProject(proj.root.string());

    REQUIRE(first.assetsScanned == 1);
    REQUIRE(first.metaFilesCreated == 1);
    REQUIRE(first.errors.empty());

    const auto firstMeta = asset::AssetMetadataSerializer::load(
        asset::AssetMetadataSerializer::getMetaPath(assetPath));
    REQUIRE(firstMeta.has_value());

    const auto second = asset::AssetDatabaseMigrator::migrateProject(proj.root.string());

    CHECK(second.assetsScanned == 1);
    CHECK(second.assetsRegistered == 0);
    CHECK(second.metaFilesCreated == 0);
    CHECK(second.errors.empty());

    const auto secondMeta = asset::AssetMetadataSerializer::load(
        asset::AssetMetadataSerializer::getMetaPath(assetPath));
    REQUIRE(secondMeta.has_value());
    CHECK(secondMeta->guid == firstMeta->guid);
    CHECK(secondMeta->type == firstMeta->type);
}

}
