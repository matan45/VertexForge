#pragma once
#include "AssetMetadata.hpp"
#include <filesystem>
#include <optional>
#include <string>

namespace asset
{
    class AssetMetadataSerializer
    {
    public:
        static bool save(const AssetMetadata& metadata, const std::filesystem::path& metaPath);
        static std::optional<AssetMetadata> load(const std::filesystem::path& metaPath);
        static std::filesystem::path getMetaPath(const std::filesystem::path& assetPath);

        static std::string assetTypeToString(resource::AssetType type);
        static resource::AssetType stringToAssetType(const std::string& str);
    };
}
