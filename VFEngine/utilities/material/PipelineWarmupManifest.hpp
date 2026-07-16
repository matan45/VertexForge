#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace material
{
    // Name of the PSO warm-up manifest entry inside an exported .vfpak (VK-1532 Phase 2).
    inline constexpr const char* kPsoManifestEntry = "pso_manifest.json";

    // Per-scene list of the material assets whose pipelines should be warmed during the
    // loading screen. Baked at export time from each scene's static material dependency
    // closure, so it also covers materials that only appear via later script-spawned prefabs
    // (which the runtime registry walk would miss). Read by the runtime PipelineWarmupAdapter.
    //
    // Keys and values are asset GUID hex strings (AssetGUID::toString), NOT file paths: GUIDs
    // are stable across export and runtime, and resolving a material GUID to a path at runtime
    // reproduces exactly the key MaterialShaderCache uses (both go through AssetDatabase::getPath).
    struct PipelineWarmupManifest
    {
        // scene GUID hex -> material GUID hex list
        std::unordered_map<std::string, std::vector<std::string>> scenes;

        std::string toJson() const
        {
            nlohmann::json scenesJson = nlohmann::json::object();
            for (const auto& [sceneGuid, materialGuids] : scenes)
            {
                scenesJson[sceneGuid] = materialGuids;
            }
            nlohmann::json root;
            root["version"] = 1;
            root["scenes"] = std::move(scenesJson);
            return root.dump();
        }

        static PipelineWarmupManifest fromJson(std::string_view json)
        {
            PipelineWarmupManifest manifest;
            const nlohmann::json root =
                nlohmann::json::parse(json.begin(), json.end(), nullptr, /*allow_exceptions*/ false);
            if (root.is_discarded() || !root.contains("scenes") || !root["scenes"].is_object())
            {
                return manifest;
            }
            for (const auto& [sceneGuid, materialGuids] : root["scenes"].items())
            {
                if (materialGuids.is_array())
                {
                    manifest.scenes[sceneGuid] = materialGuids.get<std::vector<std::string>>();
                }
            }
            return manifest;
        }
    };
}
