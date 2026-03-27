#include "DependencyScanner.hpp"
#include "AssetDatabase.hpp"
#include "../print/Log.hpp"
#include "../resource/VFSHelpers.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace asset
{
    using json = nlohmann::json;

    static void collectJsonStrings(const json& j, std::vector<std::string>& out)
    {
        if (j.is_string())
        {
            out.push_back(j.get<std::string>());
        }
        else if (j.is_object())
        {
            for (auto& [key, val] : j.items())
            {
                collectJsonStrings(val, out);
            }
        }
        else if (j.is_array())
        {
            for (const auto& item : j)
            {
                collectJsonStrings(item, out);
            }
        }
    }

    bool DependencyScanner::isAssetPath(const std::string& str)
    {
        if (str.empty() || str.size() < 4) return false;

        // Check for known asset extensions
        std::string lower = str;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        static const std::vector<std::string> assetExtensions = {
            ".vfimage", ".vfhdr", ".vfmesh", ".vfaudio", ".vfanim",
            ".vfmat", ".vfmatinstance", ".vfanimator", ".vfvfx",
            ".vfprefab", ".vfscene", ".vffont", ".vfterrain",
            ".vfterrainmat", ".vfwater", ".vfimposter"
        };

        for (const auto& ext : assetExtensions)
        {
            if (lower.size() > ext.size() && lower.substr(lower.size() - ext.size()) == ext)
            {
                return true;
            }
        }

        return false;
    }

    std::vector<std::string> DependencyScanner::extractPathsFromJson(const std::string& filePath)
    {
        std::vector<std::string> paths;

        try
        {
            json j = resource::readJsonFile(filePath);
            if (j.is_null()) return paths;

            std::vector<std::string> allStrings;
            collectJsonStrings(j, allStrings);

            for (auto& str : allStrings)
            {
                if (isAssetPath(str))
                {
                    // Normalize path
                    std::replace(str.begin(), str.end(), '\\', '/');
                    paths.push_back(str);
                }
            }
        }
        catch (const std::exception&)
        {
            // Silently skip files that can't be parsed
        }

        return paths;
    }

    void DependencyScanner::scanAsset(const AssetGUID& guid, const std::string& assetPath,
                                       const std::string& projectRoot)
    {
        auto& db = AssetDatabase::instance();

        // Determine if this is a JSON-based asset file
        std::string ext = fs::path(assetPath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        static const std::unordered_set<std::string> jsonExtensions = {
            ".vfscene", ".vfprefab", ".vfmat", ".vfmatinstance",
            ".vfanimator", ".vfvfx", ".vfterrainmat"
        };

        if (jsonExtensions.find(ext) == jsonExtensions.end()) return;

        auto referencedPaths = extractPathsFromJson(assetPath);

        std::vector<AssetGUID> deps;
        for (const auto& refPath : referencedPaths)
        {
            auto refGuid = db.getGUID(refPath);
            if (refGuid && *refGuid != guid)
            {
                deps.push_back(*refGuid);
            }
        }

        db.setDependencies(guid, deps);
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
