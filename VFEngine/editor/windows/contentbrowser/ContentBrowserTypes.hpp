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
        Animator,
        VFX,
        Prefab,
        Script,
        Font,
        Project,
        TerrainMaterial,
        Terrain,
        Navmesh,
        PhysAnim,
        Ocean,
        BehaviorTree,
        Plugin,
        InputMapping,
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
        Project = 12,
        Animator = 13,
        VFX = 14,
        Terrain = 15,
        Navmesh = 16,
        PhysAnim = 17,
        Water = 18,
        BehaviorTree = 19,
        Plugin = 20,
        InputMapping = 21
    };

    struct Asset
    {
        std::string name;
        std::string path;
        AssetType type;
        std::string extension;
        uint64_t fileSize = 0;
        int64_t lastModified = 0;
        bool isSelected = false;
        bool isCut = false;
    };

    enum class SortField : uint8_t
    {
        Name = 0,
        Date,
        Size,
        Type
    };

    struct AssetFilter
    {
        std::string searchQuery;
        AssetType typeFilter = AssetType::Other; // Other = "All"
        SortField sortBy = SortField::Name;
        bool sortAscending = true;
    };
}
