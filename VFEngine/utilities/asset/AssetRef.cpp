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

        // VK-1346: accept project-relative paths. Resolve to a normalized
        // absolute forward-slash path against the loaded project root so the
        // DB lookup hits the canonical key and any auto-register writes the
        // .vfmeta next to the real asset (not relative to the process CWD).
        std::string resolvedPath = AssetDatabase::instance().resolveAssetPath(path);
        if (resolvedPath.empty()) return invalid();

        auto guidOpt = AssetDatabase::instance().getGUID(resolvedPath);
        if (guidOpt)
        {
            return AssetRef(*guidOpt);
        }

        // Auto-register untracked assets (e.g., files copied from outside the editor)
        resource::AssetType type = AssetDatabaseMigrator::detectAssetTypeFromPath(resolvedPath);
        if (type != resource::AssetType::COUNT)
        {
            // Check if .vfmeta already exists (may have importSource/importTimestamp)
            auto metaPath = AssetMetadataSerializer::getMetaPath(resolvedPath);
            auto existingMeta = AssetMetadataSerializer::load(metaPath);

            if (existingMeta.has_value())
            {
                // Reuse existing GUID and register in database without overwriting meta
                AssetDatabase::instance().registerAssetWithGUID(
                    existingMeta->guid, resolvedPath, existingMeta->type);
                vfLogInfo("Auto-registered untracked asset from existing meta: {}", resolvedPath);
                return AssetRef(existingMeta->guid);
            }

            auto guid = AssetDatabase::instance().registerAsset(resolvedPath, type);

            AssetMetadata metadata;
            metadata.guid = guid;
            metadata.type = type;
            AssetMetadataSerializer::save(metadata, metaPath);

            vfLogInfo("Auto-registered untracked asset: {}", resolvedPath);
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
