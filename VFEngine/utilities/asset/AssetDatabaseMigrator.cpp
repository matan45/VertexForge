#include "AssetDatabaseMigrator.hpp"
#include "AssetDatabase.hpp"
#include "AssetExtensions.hpp"
#include "AssetTypeRegistry.hpp"
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
        return extensions::typeForExtension(extension);
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

                // Registry-aware (VK-1449): recognises built-in AND plugin-
                // registered extensions. Unknown extensions are still skipped
                // (graceful — no GUID/.vfmeta until a plugin claims them).
                if (!extensions::isAssetExtension(ext)) continue;

                // Check if .vfmeta already exists
                auto metaPath = AssetMetadataSerializer::getMetaPath(entry.path());
                if (fs::exists(metaPath, ec)) continue;

                resource::AssetType type = detectAssetType(ext);

                // For plugin-registered types, carry the precise typeId so the
                // asset stays classified in DB/search/export even when the
                // owning plugin is later absent.
                std::string pluginTypeId;
                if (type == resource::AssetType::PluginAsset)
                {
                    AssetTypeRecord rec;
                    if (AssetTypeRegistry::instance().findByExtension(ext, rec))
                        pluginTypeId = rec.typeId;
                }

                std::string assetPath = entry.path().string();

                // Register in database
                auto guid = AssetDatabase::instance().registerAsset(assetPath, type, "", pluginTypeId);

                // Create .vfmeta sidecar
                AssetMetadata metadata;
                metadata.guid = guid;
                metadata.type = type;
                metadata.pluginTypeId = pluginTypeId;
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
