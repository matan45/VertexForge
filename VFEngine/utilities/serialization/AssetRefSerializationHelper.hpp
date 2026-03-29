#pragma once
#include <nlohmann/json.hpp>
#include "../asset/AssetRef.hpp"
#include "../asset/AssetDatabase.hpp"
#include "../asset/AssetDatabaseMigrator.hpp"
#include "../asset/AssetMetadataSerializer.hpp"
#include "../print/Log.hpp"
#include <filesystem>
#include <string>

namespace serialization
{
    // Writes an AssetRef as GUID hex + resolved path fallback.
    // The path fallback allows scene loading even when the AssetDatabase is cold
    // (e.g., editor opened without a project).
    inline void writeAssetRef(nlohmann::json& j, const std::string& key, const asset::AssetRef& ref)
    {
        j[key] = ref.toHexString();
        const std::string& path = ref.resolve();
        if (!path.empty())
        {
            j[key + "Path"] = path;
        }
    }

    // Reads an AssetRef from JSON. Tries GUID first; if it cannot resolve,
    // falls back to the stored path (keyPath) and re-registers the asset.
    // Also supports legacy path-only values (pre-GUID migration).
    inline asset::AssetRef readAssetRef(const nlohmann::json& j, const std::string& key,
                                        const std::string& legacyKey = "")
    {
        // Try GUID key first
        if (auto it = j.find(key); it != j.end() && it->is_string())
        {
            std::string val = it->get<std::string>();
            if (!val.empty())
            {
                // Detect if value is a file path (legacy) or hex GUID
                bool isPath = val.find('.') != std::string::npos
                           || val.find('/') != std::string::npos
                           || val.find('\\') != std::string::npos;

                if (isPath)
                {
                    return asset::AssetRef::fromPath(val);
                }

                auto ref = asset::AssetRef::fromHexString(val);
                // If GUID is valid but doesn't resolve, try the stored path fallback
                if (ref.isValid() && ref.resolve().empty())
                {
                    std::string pathKey = key + "Path";
                    if (auto pathIt = j.find(pathKey); pathIt != j.end() && pathIt->is_string())
                    {
                        std::string fallbackPath = pathIt->get<std::string>();
                        if (!fallbackPath.empty())
                        {
                            auto pathRef = asset::AssetRef::fromPath(fallbackPath);
                            if (pathRef.isValid())
                            {
                                return pathRef;
                            }

                            // fromPath failed — force-register the GUID with the stored path
                            // so the original GUID remains usable (e.g., snapshot restore)
                            if (std::filesystem::exists(fallbackPath))
                            {
                                auto type = asset::AssetDatabaseMigrator::detectAssetTypeFromPath(fallbackPath);
                                asset::AssetDatabase::instance().registerAssetWithGUID(
                                    ref.getGUID(), fallbackPath,
                                    type != resource::AssetType::COUNT ? type : resource::AssetType::Texture);
                                ref.invalidateCache();
                                vfLogInfo("Re-registered missing asset GUID for: {}", fallbackPath);
                                return ref;
                            }
                        }
                    }
                }
                return ref;
            }
        }

        // Try legacy path key
        if (!legacyKey.empty())
        {
            if (auto it = j.find(legacyKey); it != j.end() && it->is_string())
            {
                std::string val = it->get<std::string>();
                if (!val.empty()) return asset::AssetRef::fromPath(val);
            }
        }

        return asset::AssetRef::invalid();
    }
}
