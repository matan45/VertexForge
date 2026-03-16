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
            auto guid = AssetDatabase::instance().registerAsset(path, type);

            AssetMetadata metadata;
            metadata.guid = guid;
            metadata.type = type;
            auto metaPath = AssetMetadataSerializer::getMetaPath(path);
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

    std::string AssetRef::resolve() const
    {
        if (!guid.isValid()) return {};

        auto pathOpt = AssetDatabase::instance().getPath(guid);
        if (pathOpt)
        {
            return *pathOpt;
        }
        return {};
    }
}
