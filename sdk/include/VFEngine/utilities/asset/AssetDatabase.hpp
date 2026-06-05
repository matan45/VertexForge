#pragma once
#include "AssetGUID.hpp"
#include "AssetMetadata.hpp"
#include "../resource/AssetTypes.hpp"
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace asset
{
    struct AssetDatabaseEntry
    {
        AssetGUID guid;
        std::string path;
        resource::AssetType type = resource::AssetType::COUNT;
        std::string importSource;
    };

    class AssetDatabase
    {
    public:
        static AssetDatabase& instance();

        AssetDatabase(const AssetDatabase&) = delete;
        AssetDatabase& operator=(const AssetDatabase&) = delete;

        // Registration
        AssetGUID registerAsset(const std::string& path, resource::AssetType type,
                                const std::string& importSource = "");
        void registerAssetWithGUID(const AssetGUID& guid, const std::string& path,
                                   resource::AssetType type, const std::string& importSource = "");
        void unregisterAsset(const AssetGUID& guid);

        // Lookups
        std::optional<std::string> getPath(const AssetGUID& guid) const;
        std::optional<AssetGUID> getGUID(const std::string& path) const;
        std::optional<AssetDatabaseEntry> getEntry(const AssetGUID& guid) const;
        std::vector<AssetDatabaseEntry> getAllAssets() const;
        std::vector<AssetDatabaseEntry> getAssetsByType(resource::AssetType type) const;
        size_t getAssetCount() const;

        // Path updates
        bool updatePath(const AssetGUID& guid, const std::string& newPath);
        bool updatePathByOldPath(const std::string& oldPath, const std::string& newPath);

        // Dependency graph
        void addDependency(const AssetGUID& from, const AssetGUID& to);
        void removeDependency(const AssetGUID& from, const AssetGUID& to);
        void clearDependencies(const AssetGUID& guid);
        void setDependencies(const AssetGUID& guid, const std::vector<AssetGUID>& deps);
        std::vector<AssetGUID> getDependencies(const AssetGUID& guid) const;
        std::vector<AssetGUID> getDependents(const AssetGUID& guid) const;

        // Persistence
        bool saveIndex(const std::string& projectRoot) const;
        bool loadIndex(const std::string& projectRoot);
        bool rebuildFromMetaFiles(const std::string& searchRoot);

        // Resolve a (possibly project-relative) path to a normalized absolute
        // forward-slash path using the project root. "" for empty input;
        // falls back to normalizePath(path) when no project root is set.
        std::string resolveAssetPath(const std::string& path) const;

        // Fallback: scan .vfmeta files for a GUID not in the database
        std::optional<std::string> tryResolveByMetaScan(const AssetGUID& guid);

        void clear();

    private:
        AssetDatabase() = default;

        std::string normalizePath(const std::string& path) const;
        std::string lastSearchRoot;
        std::string projectRoot;   // normalized project root for relative-path resolution

        std::unordered_map<AssetGUID, AssetDatabaseEntry, AssetGUID::Hash> guidToEntry;
        std::unordered_map<std::string, AssetGUID> pathToGuid;

        // Dependency graph: from -> set of targets
        std::unordered_map<AssetGUID, std::unordered_set<AssetGUID, AssetGUID::Hash>, AssetGUID::Hash> dependencies;
        // Reverse dependency graph: to -> set of dependents
        std::unordered_map<AssetGUID, std::unordered_set<AssetGUID, AssetGUID::Hash>, AssetGUID::Hash> dependents;

        mutable std::shared_mutex dbMutex;
    };
}
