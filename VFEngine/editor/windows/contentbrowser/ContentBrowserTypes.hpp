#pragma once
#include <string>
#include <cstdint>

namespace windows
{
    enum class AssetType
    {
        Texture,
        HDR,
        Model,
        Audio,
        Animation,
        Scene,
        Material,
        MaterialInstance,
        Prefab,
        Script,
        Font,
        Project,
        Other
    };

    enum class AtlasIcon : uint32_t
    {
        Animation = 0,
        Texture = 1,
        Mtype = 2,
        Mesh = 3,
        Material = 4,
        Folder = 5,
        Scene = 6,
        Hdr = 7,
        Audio = 8,
        File = 9,
        Prefab = 10,
        Font = 11,
        Project = 12
    };

    struct Asset
    {
        std::string name;
        std::string path;
        AssetType type;
        bool isSelected = false;  // For multi-selection
        bool isCut = false;       // For visual dimming of cut items
    };
}
