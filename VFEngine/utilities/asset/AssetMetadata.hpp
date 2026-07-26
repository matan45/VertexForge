#pragma once
#include "AssetGUID.hpp"
#include "../resource/AssetTypes.hpp"
#include "../config/Config.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <optional>

namespace asset
{
    struct FragmentPhysicsInfo
    {
        glm::vec3 centerOfMass{0.0f};
        float volume = 0.0f;
        glm::vec3 bboxMin{0.0f};
        glm::vec3 bboxMax{0.0f};
    };

    struct FractureMetadata
    {
        uint32_t fragmentCount = 0;
        uint32_t seedDistribution = 0;
        uint32_t randomSeed = 42;
        float innerUVScale = 1.0f;
        std::vector<FragmentPhysicsInfo> fragments;
        std::vector<std::tuple<uint32_t, uint32_t, float>> connectivity;
    };

    struct AssetMetadata
    {
        // Version 2 adds the scanned "dependencies" GUID array. Version 1
        // files load unchanged (the field is optional on read).
        // Version 3 adds the optional "pluginTypeId" string for plugin-
        // registered asset types (VK-1449). Absent on built-ins; ignored by
        // older readers, so older files load unchanged.
        // Version 4 adds the optional "importOptions" object holding the
        // importer-declared options the asset was baked with (VK-1629), so a
        // reimport can replay them. Absent on assets imported before it and on
        // in-editor-authored assets; version <= 3 files load unchanged.
        static constexpr uint32_t kCurrentFormatVersion = 4;

        AssetGUID guid;
        resource::AssetType type = resource::AssetType::COUNT;
        std::string importSourcePath;
        std::string importTimestamp;
        uint32_t formatVersion = kCurrentFormatVersion;
        std::vector<AssetGUID> dependencies;
        std::optional<FractureMetadata> fractureData;
        // For type == PluginAsset: the registered plugin type id (e.g.
        // "gas.ability"). Empty for built-in types.
        std::string pluginTypeId;
        // The importer-declared options (import::ImportOptionDesc::key ->
        // chosen value) this asset was baked with, so the content browser can
        // reimport it without asking again. Empty for assets whose importer
        // declares no options, and for anything authored in the editor.
        std::map<std::string, importConfig::ImportOptionValue> importOptions;
    };
}
