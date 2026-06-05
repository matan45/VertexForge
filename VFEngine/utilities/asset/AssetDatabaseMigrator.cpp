#include "AssetDatabaseMigrator.hpp"
#include "AssetDatabase.hpp"
#include "AssetMetadataSerializer.hpp"
#include "../print/Log.hpp"
#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace asset
{
    resource::AssetType AssetDatabaseMigrator::detectAssetType(const std::string& extension)
    {
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".vfimage")         return resource::AssetType::Texture;
        if (ext == ".vfhdr")           return resource::AssetType::HDR;
        if (ext == ".vfmesh")          return resource::AssetType::Mesh;
        if (ext == ".vfaudio")         return resource::AssetType::Audio;
        if (ext == ".vfanim")          return resource::AssetType::Animation;
        if (ext == ".vfmat")           return resource::AssetType::Material;
        if (ext == ".vfmatinstance")    return resource::AssetType::MaterialInstance;
        if (ext == ".vfanimator")      return resource::AssetType::Animator;
        if (ext == ".vfvfx")           return resource::AssetType::VFX;
        if (ext == ".vffont")          return resource::AssetType::Font;
        if (ext == ".vfnavmesh")       return resource::AssetType::Navmesh;
        if (ext == ".vfnavindex")      return resource::AssetType::Navmesh;
        if (ext == ".vfinputmapping") return resource::AssetType::InputMapping;
        if (ext == ".vfterrain")      return resource::AssetType::Terrain;
        if (ext == ".vfterrainmat")   return resource::AssetType::TerrainMaterial;
        if (ext == ".vfbehaviortree") return resource::AssetType::BehaviorTree;
        if (ext == ".vfphysanim")  return resource::AssetType::PhysicsShape;
        if (ext == ".vfscene")     return resource::AssetType::Scene;
        if (ext == ".vfsettings")  return resource::AssetType::Scene;
        if (ext == ".mt")          return resource::AssetType::Script;
        return resource::AssetType::COUNT;
    }

    resource::AssetType AssetDatabaseMigrator::detectAssetTypeFromPath(const std::string& filePath)
    {
        std::string ext = std::filesystem::path(filePath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return detectAssetType(ext);
    }

    AssetDatabaseMigrator::MigrationResult AssetDatabaseMigrator::migrateProject(const std::string& projectRoot)
    {
        MigrationResult result;

        static const std::unordered_set<std::string> assetExtensions = {
            ".vfimage", ".vfhdr", ".vfmesh", ".vfaudio", ".vfanim",
            ".vfmat", ".vfmatinstance", ".vfanimator", ".vfvfx",
            ".vffont", ".vfscene", ".vfsettings", ".vfprefab", ".vfterrain",
            ".vfterrainmat", ".vfwater", ".vfnavmesh", ".vfnavindex", ".vfimposter",
            ".vfinputmapping", ".vfbehaviortree", ".vfphysanim",
            ".mt"
        };

        try
        {
            std::error_code ec;
            for (const auto& entry : fs::recursive_directory_iterator(
                     projectRoot, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec)
                {
                    ec.clear();
                    continue;
                }

                if (!entry.is_regular_file()) continue;

                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (assetExtensions.find(ext) == assetExtensions.end()) continue;

                // Check if .vfmeta already exists
                auto metaPath = AssetMetadataSerializer::getMetaPath(entry.path());
                if (fs::exists(metaPath, ec)) continue;

                resource::AssetType type = detectAssetType(ext);
                std::string assetPath = entry.path().string();

                // Register in database
                auto guid = AssetDatabase::instance().registerAsset(assetPath, type);

                // Create .vfmeta sidecar
                AssetMetadata metadata;
                metadata.guid = guid;
                metadata.type = type;
                if (AssetMetadataSerializer::save(metadata, metaPath))
                {
                    result.metaFilesCreated++;
                }
                else
                {
                    result.errors.push_back("Failed to create meta file: " + metaPath.string());
                }

                result.assetsRegistered++;
            }
        }
        catch (const std::exception& e)
        {
            result.errors.push_back(std::string("Migration error: ") + e.what());
            vfLogError("Asset database migration failed: {}", e.what());
        }

        vfLogInfo("Migration complete: {} assets registered, {} meta files created",
                  result.assetsRegistered, result.metaFilesCreated);
        return result;
    }
}
