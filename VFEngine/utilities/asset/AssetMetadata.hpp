#pragma once
#include "AssetGUID.hpp"
#include "../resource/AssetTypes.hpp"
#include <string>

namespace asset
{
    struct AssetMetadata
    {
        AssetGUID guid;
        resource::AssetType type = resource::AssetType::COUNT;
        std::string importSourcePath;
        std::string importTimestamp;
        uint32_t formatVersion = 1;
    };
}
