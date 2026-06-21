#pragma once
#include "../resource/AssetTypes.hpp"
#include <algorithm>
#include <string>
#include <unordered_set>

namespace asset
{
    // Single source of truth for asset file extensions. DependencyScanner and
    // AssetDatabaseMigrator both derive their extension sets from here so the
    // lists cannot drift apart again.
    namespace extensions
    {
        // Lowercase extension (".vfimage") -> AssetType. COUNT for unknown.
        inline resource::AssetType typeForExtension(const std::string& extension)
        {
            std::string ext = extension;
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == ".vfimage")        return resource::AssetType::Texture;
            if (ext == ".vfhdr")          return resource::AssetType::HDR;
            if (ext == ".vfmesh")         return resource::AssetType::Mesh;
            if (ext == ".vfaudio")        return resource::AssetType::Audio;
            if (ext == ".vfanim")         return resource::AssetType::Animation;
            if (ext == ".vfmat")          return resource::AssetType::Material;
            if (ext == ".vfmatinstance")  return resource::AssetType::MaterialInstance;
            if (ext == ".vfanimator")     return resource::AssetType::Animator;
            if (ext == ".vfvfx")          return resource::AssetType::VFX;
            if (ext == ".vfvfxsequence")  return resource::AssetType::VFXSequence;
            if (ext == ".vffont")         return resource::AssetType::Font;
            if (ext == ".vfnavmesh")      return resource::AssetType::Navmesh;
            if (ext == ".vfnavindex")     return resource::AssetType::Navmesh;
            if (ext == ".vfinputmapping") return resource::AssetType::InputMapping;
            if (ext == ".vfterrain")      return resource::AssetType::Terrain;
            if (ext == ".vfterrainmat")   return resource::AssetType::TerrainMaterial;
            if (ext == ".vfbehaviortree") return resource::AssetType::BehaviorTree;
            if (ext == ".vfphysanim")     return resource::AssetType::PhysicsShape;
            if (ext == ".vfscene")        return resource::AssetType::Scene;
            if (ext == ".vfsettings")     return resource::AssetType::Scene;
            if (ext == ".vftheme")        return resource::AssetType::Theme;
            if (ext == ".vfprefab")       return resource::AssetType::Prefab;
            if (ext == ".vfrig")          return resource::AssetType::HumanoidRig;
            if (ext == ".vfretarget")     return resource::AssetType::RetargetMap;
            if (ext == ".mt")             return resource::AssetType::Script;
            return resource::AssetType::COUNT;
        }

        // Every extension the asset database tracks (registration + migration).
        // .vfwater/.vfimposter have no AssetType yet but are still referenced
        // by scenes, so they stay registrable as dependency targets.
        inline const std::unordered_set<std::string>& allAssetExtensions()
        {
            static const std::unordered_set<std::string> set = {
                ".vfimage", ".vfhdr", ".vfmesh", ".vfaudio", ".vfanim",
                ".vfmat", ".vfmatinstance", ".vfanimator", ".vfvfx",
                ".vfvfxsequence",
                ".vffont", ".vfscene", ".vfsettings", ".vfprefab", ".vftheme",
                ".vfterrain", ".vfterrainmat", ".vfwater", ".vfnavmesh",
                ".vfnavindex", ".vfimposter", ".vfinputmapping",
                ".vfbehaviortree", ".vfphysanim", ".vfrig", ".vfretarget", ".mt"
            };
            return set;
        }

        // JSON-based asset files that can reference other assets — the set the
        // DependencyScanner parses. Binary formats stay excluded for speed;
        // unparsable files are skipped silently, so over-inclusion is safe.
        inline const std::unordered_set<std::string>& jsonContainerExtensions()
        {
            static const std::unordered_set<std::string> set = {
                ".vfscene", ".vfsettings", ".vfprefab", ".vfmat",
                ".vfmatinstance", ".vfanimator", ".vfvfx", ".vfvfxsequence",
                ".vfterrainmat",
                ".vftheme", ".vfbehaviortree", ".vfinputmapping",
                ".vfrig", ".vfretarget"
            };
            return set;
        }

        inline bool isAssetExtension(const std::string& extension)
        {
            std::string ext = extension;
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return allAssetExtensions().count(ext) > 0;
        }

        inline bool isJsonContainerExtension(const std::string& extension)
        {
            std::string ext = extension;
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return jsonContainerExtensions().count(ext) > 0;
        }
    }
}
