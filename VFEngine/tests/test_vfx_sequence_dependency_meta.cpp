#include <doctest.h>

#include <asset/AssetDatabase.hpp>
#include <asset/AssetMetadata.hpp>
#include <asset/AssetMetadataSerializer.hpp>
#include <asset/AssetRef.hpp>
#include <asset/DependencyScanner.hpp>
#include <vfx/VFXSequenceAsset.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    struct TempSequenceProject
    {
        fs::path root;

        TempSequenceProject()
            : root(fs::temp_directory_path() / "vf_vfx_sequence_dependency_meta_tests")
        {
            std::error_code ec;
            fs::remove_all(root, ec);
            fs::create_directories(root, ec);
            asset::AssetDatabase::instance().clear();
            REQUIRE(asset::AssetDatabase::instance().rebuildFromMetaFiles(root.string()));
        }

        ~TempSequenceProject()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
            asset::AssetDatabase::instance().clear();
        }

        fs::path writeFile(const std::string& relPath, const std::string& content) const
        {
            fs::path path = root / relPath;
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            std::ofstream file(path);
            file << content;
            return path;
        }
    };

    bool containsGuid(const std::vector<asset::AssetGUID>& guids, const asset::AssetGUID& guid)
    {
        return std::find(guids.begin(), guids.end(), guid) != guids.end();
    }
}

TEST_CASE("VFX sequence dependency scanner writes child VFX dependencies to metadata")
{
    TempSequenceProject project;
    auto& db = asset::AssetDatabase::instance();

    const fs::path childAPath = project.writeFile("vfx/child_a.vfVFX", "{}");
    const fs::path childBPath = project.writeFile("vfx/child_b.vfVFX", "{}");
    const asset::AssetGUID childAGuid =
        db.registerAsset(childAPath.string(), resource::AssetType::VFX);
    const asset::AssetGUID childBGuid =
        db.registerAsset(childBPath.string(), resource::AssetType::VFX);

    vfx::VFXSequenceData data;
    data.name = "DependencyCombo";
    {
        vfx::VFXSequenceStep step;
        step.vfxRef = asset::AssetRef::fromGUID(childAGuid);
        data.steps.push_back(step);
    }
    {
        vfx::VFXSequenceStep step;
        step.vfxRef = asset::AssetRef::fromGUID(childBGuid);
        data.steps.push_back(step);
    }

    const fs::path sequencePath = project.root / "vfx" / "combo.vfVFXSequence";
    REQUIRE(vfx::VFXSequenceAsset::save(data, sequencePath.string()));
    const asset::AssetGUID sequenceGuid =
        db.registerAsset(sequencePath.string(), resource::AssetType::VFXSequence);

    asset::AssetMetadata metadata;
    metadata.guid = sequenceGuid;
    metadata.type = resource::AssetType::VFXSequence;
    REQUIRE(asset::AssetMetadataSerializer::save(
        metadata, asset::AssetMetadataSerializer::getMetaPath(sequencePath.string())));

    const auto deps =
        asset::DependencyScanner::scanAsset(sequenceGuid, sequencePath.string(), project.root.string());

    REQUIRE(deps.size() == 2);
    CHECK(containsGuid(deps, childAGuid));
    CHECK(containsGuid(deps, childBGuid));
    CHECK_FALSE(containsGuid(deps, sequenceGuid));

    const auto dbDeps = db.getDependencies(sequenceGuid);
    CHECK(containsGuid(dbDeps, childAGuid));
    CHECK(containsGuid(dbDeps, childBGuid));

    const auto loadedMeta =
        asset::AssetMetadataSerializer::load(asset::AssetMetadataSerializer::getMetaPath(sequencePath.string()));
    REQUIRE(loadedMeta.has_value());
    CHECK(containsGuid(loadedMeta->dependencies, childAGuid));
    CHECK(containsGuid(loadedMeta->dependencies, childBGuid));
}
