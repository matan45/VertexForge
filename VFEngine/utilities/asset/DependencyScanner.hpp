#pragma once
#include "AssetGUID.hpp"
#include <string>
#include <vector>

namespace asset
{
    class DependencyScanner
    {
    public:
        // Scan all registered assets and rebuild the full dependency graph
        static void scanAll(const std::string& projectRoot);

        // Scan a single asset and update its dependencies
        static void scanAsset(const AssetGUID& guid, const std::string& assetPath,
                              const std::string& projectRoot);

    private:
        // Extract all path strings from a JSON file
        static std::vector<std::string> extractPathsFromJson(const std::string& filePath);

        // Check if a string looks like an asset path
        static bool isAssetPath(const std::string& str);
    };
}
