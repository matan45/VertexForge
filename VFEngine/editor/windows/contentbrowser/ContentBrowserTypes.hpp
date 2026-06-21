#pragma once
#include "resource/AssetTypes.hpp"
#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>
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
        Retarget,
        VFXSequence,
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
        InputMapping = 21,
        Retarget = 22, // shared by .vfrig and .vfretarget (VK-910)
        VFXSequence = 23 // .vfVFXSequence combo asset (VK-1425)
    };

    // Canonical per-type display data. Single source of truth for the filter
    // dropdown, grid icons and (future) type badges — keep it covering every
    // AssetType value; test_contentbrowser_types.cpp enforces completeness.
    struct AssetTypeInfo
    {
        AssetType type;
        const char* label;
        AtlasIcon icon;
        uint32_t badgeColor; // IM_COL32 layout (0xAABBGGRR)
    };

    inline const std::array<AssetTypeInfo, 25>& assetTypeTable()
    {
        using enum AssetType;
        static const std::array<AssetTypeInfo, 25> table = {{
            {Texture,          "Texture",           AtlasIcon::Texture,      0xFFF7C34F},
            {HDR,              "HDR",               AtlasIcon::Hdr,          0xFFF7E04F},
            {Model,            "Model",             AtlasIcon::Mesh,         0xFF4FC3F7},
            {Audio,            "Audio",             AtlasIcon::Audio,        0xFF4FF78A},
            {Animation,        "Animation",         AtlasIcon::Animation,    0xFFB04FF7},
            {Scene,            "Scene",             AtlasIcon::Scene,        0xFF4F6EF7},
            {Material,         "Material",          AtlasIcon::Material,     0xFF4FF7DD},
            {MaterialInstance, "Material Instance", AtlasIcon::Material,     0xFF4FD7C0},
            {Animator,         "Animator",          AtlasIcon::Animator,     0xFF914FF7},
            {VFX,              "VFX",               AtlasIcon::VFX,          0xFFF74F9E},
            {Prefab,           "Prefab",            AtlasIcon::Prefab,       0xFF4F9EF7},
            {Script,           "Script",            AtlasIcon::Mtype,        0xFF8AF74F},
            {Font,             "Font",              AtlasIcon::Font,         0xFFC0C0C0},
            {Project,          "Project",           AtlasIcon::Project,      0xFFE0E0E0},
            {TerrainMaterial,  "Terrain Material",  AtlasIcon::Material,     0xFF4FB78A},
            {Terrain,          "Terrain",           AtlasIcon::Terrain,      0xFF4F8A5E},
            {Navmesh,          "Navmesh",           AtlasIcon::Navmesh,      0xFF6EC0F7},
            {PhysAnim,         "Phys Anim",         AtlasIcon::PhysAnim,     0xFFC04FF7},
            {Ocean,            "Ocean",             AtlasIcon::Water,        0xFFF7B84F},
            {BehaviorTree,     "Behavior Tree",     AtlasIcon::BehaviorTree, 0xFF4FF74F},
            {Plugin,           "Plugin",            AtlasIcon::Plugin,       0xFFF74F4F},
            {InputMapping,     "Input Mapping",     AtlasIcon::InputMapping, 0xFFAAAAF7},
            {Retarget,         "Retarget",          AtlasIcon::Retarget,     0xFFD08AF7},
            {VFXSequence,      "VFX Sequence",      AtlasIcon::VFXSequence,  0xFFF74FC8},
            {Other,            "Other",             AtlasIcon::File,         0xFF909090}
        }};
        return table;
    }

    inline const AssetTypeInfo& assetTypeInfo(AssetType type)
    {
        for (const auto& info : assetTypeTable())
        {
            if (info.type == type)
                return info;
        }
        return assetTypeTable().back(); // Other
    }

    inline std::string formatFileSize(uint64_t bytes)
    {
        static constexpr std::array<const char*, 5> units = {"B", "KB", "MB", "GB", "TB"};
        double value = static_cast<double>(bytes);
        size_t unitIndex = 0;

        while (value >= 1024.0 && unitIndex + 1 < units.size())
        {
            value /= 1024.0;
            ++unitIndex;
        }

        if (unitIndex == 0)
            return std::to_string(bytes) + " B";

        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unitIndex]);
        return buffer;
    }

    // The asset database speaks resource::AssetType; the browser has its own
    // enum (Ocean/Project/Plugin exist only here, Skeleton/World/Theme only
    // there). Total mapping — anything without a browser counterpart is Other.
    inline AssetType fromResourceType(resource::AssetType type)
    {
        switch (type)
        {
        case resource::AssetType::Texture:          return AssetType::Texture;
        case resource::AssetType::Mesh:             return AssetType::Model;
        case resource::AssetType::Audio:            return AssetType::Audio;
        case resource::AssetType::Animation:        return AssetType::Animation;
        case resource::AssetType::Animator:         return AssetType::Animator;
        case resource::AssetType::Material:         return AssetType::Material;
        case resource::AssetType::MaterialInstance: return AssetType::MaterialInstance;
        case resource::AssetType::PhysicsShape:     return AssetType::PhysAnim;
        case resource::AssetType::VFX:              return AssetType::VFX;
        case resource::AssetType::VFXSequence:      return AssetType::VFXSequence;
        case resource::AssetType::Script:           return AssetType::Script;
        case resource::AssetType::HDR:              return AssetType::HDR;
        case resource::AssetType::Font:             return AssetType::Font;
        case resource::AssetType::Navmesh:          return AssetType::Navmesh;
        case resource::AssetType::InputMapping:     return AssetType::InputMapping;
        case resource::AssetType::Terrain:          return AssetType::Terrain;
        case resource::AssetType::TerrainMaterial:  return AssetType::TerrainMaterial;
        case resource::AssetType::BehaviorTree:     return AssetType::BehaviorTree;
        case resource::AssetType::Scene:            return AssetType::Scene;
        case resource::AssetType::Prefab:           return AssetType::Prefab;
        case resource::AssetType::HumanoidRig:      return AssetType::Retarget;
        case resource::AssetType::RetargetMap:      return AssetType::Retarget;
        case resource::AssetType::Skeleton:
        case resource::AssetType::World:
        case resource::AssetType::Theme:
        default:                                    return AssetType::Other;
        }
    }

    struct Asset
    {
        std::string name;
        std::string path;
        AssetType type;
        std::string extension;
        uint64_t fileSize = 0;
        bool fileSizeKnown = false;
        int64_t lastModified = 0;
        bool isDirectory = false;
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
        std::optional<AssetType> typeFilter; // nullopt = "All"
        SortField sortBy = SortField::Name;
        bool sortAscending = true;
    };

    // searchQuery resolved once per change (asset-database lookups for guid:
    // and ref: are too expensive per asset). Structured tokens constrain files
    // only — folders stay visible for navigation, like the type dropdown.
    struct ResolvedAssetFilter
    {
        std::vector<std::string> terms;                // lowercased, AND-ed against names
        std::optional<AssetType> queryType;            // from type: token
        std::string extToken;                          // lowercased, with dot
        std::string guidPath;                          // normalized path the guid: token resolves to
        bool hasGuidToken = false;
        std::unordered_set<std::string> refMatchPaths; // normalized paths referencing the ref: target
        bool hasRefToken = false;
        bool matchNothing = false;                     // unresolvable token -> no file matches
    };
}
