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

        // Scan a single asset, update its dependencies in the AssetDatabase and
        // its .vfmeta sidecar (rewritten only when the dependency list changed).
        // Returns the computed dependency list (empty for non-container assets).
        static std::vector<AssetGUID> scanAsset(const AssetGUID& guid, const std::string& assetPath,
                                                const std::string& projectRoot);

    private:
        // Extract referenced asset GUIDs from a JSON file: path strings with a
        // known asset extension, plus strict 16-hex GUID strings that resolve
        // in the AssetDatabase (covers GUID-only refs with no path fallback).
        static std::vector<AssetGUID> extractDependenciesFromJson(const AssetGUID& selfGuid,
                                                                  const std::string& filePath);

        // Check if a string looks like an asset path
        static bool isAssetPath(const std::string& str);

        // Persist the dependency list into the asset's .vfmeta (formatVersion 2)
        static void updateMetaDependencies(const std::string& assetPath,
                                           std::vector<AssetGUID> deps);
    };
}
