#include "DependencyScanner.hpp"
#include "AssetDatabase.hpp"
#include "AssetExtensions.hpp"
#include "AssetMetadataSerializer.hpp"
#include "../print/Log.hpp"
#include "../resource/VFSHelpers.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace asset
{
    using json = nlohmann::json;

    namespace
    {
        struct KeyedString
        {
            std::string key;
            std::string value;
        };

        // Array elements inherit the enclosing object's key so a GUID inside
        // ["<hex>", ...] under "dependencies"/"textureRefs" keeps its context.
        void collectJsonStrings(const json& j, const std::string& key, std::vector<KeyedString>& out)
        {
            if (j.is_string())
            {
                out.push_back({key, j.get<std::string>()});
            }
            else if (j.is_object())
            {
                for (auto& [childKey, val] : j.items())
                {
                    collectJsonStrings(val, childKey, out);
                }
            }
            else if (j.is_array())
            {
                for (const auto& item : j)
                {
                    collectJsonStrings(item, key, out);
                }
            }
        }

        bool keyLooksLikeAssetRef(const std::string& key)
        {
            return key.size() >= 3 && key.compare(key.size() - 3, 3, "Ref") == 0;
        }
    }

    bool DependencyScanner::isAssetPath(const std::string& str)
    {
        if (str.size() < 4) return false;

        auto dot = str.find_last_of('.');
        if (dot == std::string::npos || dot == 0) return false;

        return extensions::isAssetExtension(str.substr(dot));
    }

    std::vector<AssetGUID> DependencyScanner::extractDependenciesFromJson(const AssetGUID& selfGuid,
                                                                          const std::string& filePath)
    {
        std::vector<AssetGUID> deps;
        std::unordered_set<AssetGUID, AssetGUID::Hash> seen;
        auto& db = AssetDatabase::instance();

        auto addDep = [&](const AssetGUID& guid)
        {
            if (guid == selfGuid) return;
            if (seen.insert(guid).second) deps.push_back(guid);
        };

        try
        {
            json j = resource::readJsonFile(filePath);
            if (j.is_null()) return deps;

            std::vector<KeyedString> strings;
            collectJsonStrings(j, "", strings);

            for (const auto& [key, value] : strings)
            {
                if (value.empty()) continue;

                if (isAssetPath(value))
                {
                    // resolveAssetPath normalizes separators and anchors
                    // project-relative refs to the project root
                    auto refGuid = db.getGUID(db.resolveAssetPath(value));
                    if (refGuid) addDep(*refGuid);
                    continue;
                }

                if (AssetGUID::isStrictHex16(value))
                {
                    AssetGUID guid = AssetGUID::fromString(value);
                    if (!guid.isValid()) continue;

                    if (db.getEntry(guid).has_value())
                    {
                        addDep(guid);
                    }
                    else if (keyLooksLikeAssetRef(key))
                    {
                        vfLogWarning("Dangling asset reference in {}: {}={}", filePath, key, value);
                    }
                }
            }
        }
        catch (const std::exception&)
        {
            // Silently skip files that can't be parsed
        }

        return deps;
    }

    void DependencyScanner::updateMetaDependencies(const std::string& assetPath,
                                                   std::vector<AssetGUID> deps)
    {
        auto metaPath = AssetMetadataSerializer::getMetaPath(assetPath);
        auto metadata = AssetMetadataSerializer::load(metaPath);
        if (!metadata) return;

        std::sort(deps.begin(), deps.end());
        auto existing = metadata->dependencies;
        std::sort(existing.begin(), existing.end());
        if (existing == deps) return;

        metadata->dependencies = std::move(deps);
        metadata->formatVersion = AssetMetadata::kCurrentFormatVersion;
        AssetMetadataSerializer::save(*metadata, metaPath);
    }

    std::vector<AssetGUID> DependencyScanner::scanAsset(const AssetGUID& guid, const std::string& assetPath,
                                                        const std::string& projectRoot)
    {
        (void)projectRoot;

        std::string ext = fs::path(assetPath).extension().string();
        if (!extensions::isJsonContainerExtension(ext)) return {};

        auto deps = extractDependenciesFromJson(guid, assetPath);

        AssetDatabase::instance().setDependencies(guid, deps);
        updateMetaDependencies(assetPath, deps);

        return deps;
    }

    void DependencyScanner::scanAll(const std::string& projectRoot)
    {
        auto& db = AssetDatabase::instance();
        auto allAssets = db.getAllAssets();

        uint32_t scanned = 0;
        for (const auto& entry : allAssets)
        {
            scanAsset(entry.guid, entry.path, projectRoot);
            scanned++;
        }

        vfLogInfo("Dependency scan complete: scanned {} assets", scanned);
    }
}
