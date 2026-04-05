#include "AssetRef.hpp"
#include "AssetDatabase.hpp"
#include "AssetMetadataSerializer.hpp"
#include "AssetDatabaseMigrator.hpp"
#include "../print/Log.hpp"

namespace asset
{
    AssetRef AssetRef::fromPath(const std::string& path)
    {
        if (path.empty()) return invalid();

        auto guidOpt = AssetDatabase::instance().getGUID(path);
        if (guidOpt)
        {
            return AssetRef(*guidOpt);
        }

        // Auto-register untracked assets (e.g., files copied from outside the editor)
        resource::AssetType type = AssetDatabaseMigrator::detectAssetTypeFromPath(path);
        if (type != resource::AssetType::COUNT)
        {
            // Check if .vfmeta already exists (may have importSource/importTimestamp)
            auto metaPath = AssetMetadataSerializer::getMetaPath(path);
            auto existingMeta = AssetMetadataSerializer::load(metaPath);

            if (existingMeta.has_value())
            {
                // Reuse existing GUID and register in database without overwriting meta
                AssetDatabase::instance().registerAssetWithGUID(
                    existingMeta->guid, path, existingMeta->type);
                vfLogInfo("Auto-registered untracked asset from existing meta: {}", path);
                return AssetRef(existingMeta->guid);
            }

            auto guid = AssetDatabase::instance().registerAsset(path, type);

            AssetMetadata metadata;
            metadata.guid = guid;
            metadata.type = type;
            AssetMetadataSerializer::save(metadata, metaPath);

            vfLogInfo("Auto-registered untracked asset: {}", path);
            return AssetRef(guid);
        }

        return invalid();
    }

    AssetRef AssetRef::fromHexString(const std::string& hex)
    {
        if (hex.empty()) return invalid();
        return AssetRef(AssetGUID::fromString(hex));
    }

    const std::string& AssetRef::resolve() const
    {
        if (cacheValid)
            return cachedPath;

        if (!guid.isValid())
        {
            cachedPath.clear();
            cacheValid = true;
            return cachedPath;
        }

        auto pathOpt = AssetDatabase::instance().getPath(guid);
        if (!pathOpt)
        {
            // Fallback: scan .vfmeta files for this GUID
            pathOpt = AssetDatabase::instance().tryResolveByMetaScan(guid);
        }

        if (pathOpt)
        {
            cachedPath = *pathOpt;
            cacheValid = true;
        }
        else
        {
            // Don't cache failed lookups — the database may be populated later
            cachedPath.clear();
        }
        return cachedPath;
    }
}
