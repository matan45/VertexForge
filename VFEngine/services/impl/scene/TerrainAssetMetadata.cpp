#include "TerrainAssetMetadata.hpp"

#include "print/Log.hpp"
#include <asset/AssetDatabase.hpp>
#include <asset/AssetMetadata.hpp>
#include <asset/AssetMetadataSerializer.hpp>
#include <resource/AssetTypes.hpp>

#include <chrono>
#include <iomanip>
#include <sstream>

namespace services
{
    asset::AssetGUID peekOrMintTerrainGuid(const std::string& terrainPath)
    {
        const auto metaPath = asset::AssetMetadataSerializer::getMetaPath(terrainPath);
        if (const auto existingMeta = asset::AssetMetadataSerializer::load(metaPath))
        {
            if (existingMeta->guid.isValid())
                return existingMeta->guid;
        }

        // Falls back to the database before minting: a rescan can have registered the asset
        // without the caller ever holding its GUID, and minting a second one would leave the
        // layer sidecar pointing at an identity the .vfmeta then contradicts.
        if (const auto known = asset::AssetDatabase::instance().getGUID(terrainPath))
            return *known;

        return asset::AssetGUID::generate();
    }

    asset::AssetGUID refreshTerrainSidecar(const std::string& terrainPath, asset::AssetGUID minted)
    {
        auto& db = asset::AssetDatabase::instance();
        auto metaPath = asset::AssetMetadataSerializer::getMetaPath(terrainPath);
        auto existingMeta = asset::AssetMetadataSerializer::load(metaPath);

        asset::AssetMetadata metadata;
        if (existingMeta.has_value())
        {
            metadata.guid = existingMeta->guid;
        }
        else if (minted.isValid())
        {
            // The save already stamped this value into the layer sidecar, so adopting it here is
            // what keeps the two agreeing on a terrain's first save.
            metadata.guid = minted;
        }
        else
        {
            metadata.guid = asset::AssetGUID::generate();
        }
        metadata.type = resource::AssetType::Terrain;
        metadata.importSourcePath = terrainPath;
        {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
            localtime_s(&tm, &time);
            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
            metadata.importTimestamp = oss.str();
        }

        if (!asset::AssetMetadataSerializer::save(metadata, metaPath))
        {
            vfLogWarning("TerrainService: Failed to write terrain metadata sidecar {}",
                         metaPath.string());
            return {};
        }

        if (!db.getGUID(terrainPath).has_value())
        {
            db.registerAssetWithGUID(metadata.guid, terrainPath, resource::AssetType::Terrain);
        }

        return metadata.guid;
    }
}
