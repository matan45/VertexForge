#include "AssetDatabase.hpp"
#include "AssetMetadataSerializer.hpp"
#include "../print/Log.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace asset
{
    using json = nlohmann::json;

    AssetDatabase& AssetDatabase::instance()
    {
        static AssetDatabase db;
        return db;
    }

    std::string AssetDatabase::normalizePath(const std::string& path) const
    {
        std::string normalized = path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        while (!normalized.empty() && normalized.back() == '/')
        {
            normalized.pop_back();
        }
        return normalized;
    }

    AssetGUID AssetDatabase::registerAsset(const std::string& path, resource::AssetType type,
                                           const std::string& importSource)
    {
        std::string normalizedPath = normalizePath(path);

        std::unique_lock lock(dbMutex);

        // Check if already registered
        auto it = pathToGuid.find(normalizedPath);
        if (it != pathToGuid.end())
        {
            return it->second;
        }

        AssetGUID guid = AssetGUID::generate();

        AssetDatabaseEntry entry;
        entry.guid = guid;
        entry.path = normalizedPath;
        entry.type = type;
        entry.importSource = importSource;

        guidToEntry[guid] = entry;
        pathToGuid[normalizedPath] = guid;

        return guid;
    }

    void AssetDatabase::registerAssetWithGUID(const AssetGUID& guid, const std::string& path,
                                              resource::AssetType type, const std::string& importSource)
    {
        std::string normalizedPath = normalizePath(path);

        std::unique_lock lock(dbMutex);

        AssetDatabaseEntry entry;
        entry.guid = guid;
        entry.path = normalizedPath;
        entry.type = type;
        entry.importSource = importSource;

        guidToEntry[guid] = entry;
        pathToGuid[normalizedPath] = guid;
    }

    void AssetDatabase::unregisterAsset(const AssetGUID& guid)
    {
        std::unique_lock lock(dbMutex);

        auto it = guidToEntry.find(guid);
        if (it == guidToEntry.end()) return;

        pathToGuid.erase(it->second.path);
        guidToEntry.erase(it);

        // Clean up dependency graph
        if (auto depIt = dependencies.find(guid); depIt != dependencies.end())
        {
            for (const auto& target : depIt->second)
            {
                if (auto revIt = dependents.find(target); revIt != dependents.end())
                {
                    revIt->second.erase(guid);
                    if (revIt->second.empty()) dependents.erase(revIt);
                }
            }
            dependencies.erase(depIt);
        }

        if (auto revIt = dependents.find(guid); revIt != dependents.end())
        {
            for (const auto& source : revIt->second)
            {
                if (auto depIt = dependencies.find(source); depIt != dependencies.end())
                {
                    depIt->second.erase(guid);
                    if (depIt->second.empty()) dependencies.erase(depIt);
                }
            }
            dependents.erase(revIt);
        }
    }

    std::optional<std::string> AssetDatabase::getPath(const AssetGUID& guid) const
    {
        std::shared_lock lock(dbMutex);
        auto it = guidToEntry.find(guid);
        if (it == guidToEntry.end()) return std::nullopt;
        return it->second.path;
    }

    std::optional<AssetGUID> AssetDatabase::getGUID(const std::string& path) const
    {
        std::string normalizedPath = normalizePath(path);
        std::shared_lock lock(dbMutex);
        auto it = pathToGuid.find(normalizedPath);
        if (it == pathToGuid.end()) return std::nullopt;
        return it->second;
    }

    std::optional<AssetDatabaseEntry> AssetDatabase::getEntry(const AssetGUID& guid) const
    {
        std::shared_lock lock(dbMutex);
        auto it = guidToEntry.find(guid);
        if (it == guidToEntry.end()) return std::nullopt;
        return it->second;
    }

    std::vector<AssetDatabaseEntry> AssetDatabase::getAllAssets() const
    {
        std::shared_lock lock(dbMutex);
        std::vector<AssetDatabaseEntry> result;
        result.reserve(guidToEntry.size());
        for (const auto& [guid, entry] : guidToEntry)
        {
            result.push_back(entry);
        }
        return result;
    }

    std::vector<AssetDatabaseEntry> AssetDatabase::getAssetsByType(resource::AssetType type) const
    {
        std::shared_lock lock(dbMutex);
        std::vector<AssetDatabaseEntry> result;
        for (const auto& [guid, entry] : guidToEntry)
        {
            if (entry.type == type) result.push_back(entry);
        }
        return result;
    }

    size_t AssetDatabase::getAssetCount() const
    {
        std::shared_lock lock(dbMutex);
        return guidToEntry.size();
    }

    bool AssetDatabase::updatePath(const AssetGUID& guid, const std::string& newPath)
    {
        std::string normalizedNew = normalizePath(newPath);

        std::unique_lock lock(dbMutex);

        auto it = guidToEntry.find(guid);
        if (it == guidToEntry.end()) return false;

        pathToGuid.erase(it->second.path);
        it->second.path = normalizedNew;
        pathToGuid[normalizedNew] = guid;

        return true;
    }

    bool AssetDatabase::updatePathByOldPath(const std::string& oldPath, const std::string& newPath)
    {
        std::string normalizedOld = normalizePath(oldPath);
        std::string normalizedNew = normalizePath(newPath);

        std::unique_lock lock(dbMutex);

        auto it = pathToGuid.find(normalizedOld);
        if (it == pathToGuid.end()) return false;

        AssetGUID guid = it->second;
        pathToGuid.erase(it);
        pathToGuid[normalizedNew] = guid;
        guidToEntry[guid].path = normalizedNew;

        return true;
    }

    void AssetDatabase::addDependency(const AssetGUID& from, const AssetGUID& to)
    {
        std::unique_lock lock(dbMutex);
        dependencies[from].insert(to);
        dependents[to].insert(from);
    }

    void AssetDatabase::removeDependency(const AssetGUID& from, const AssetGUID& to)
    {
        std::unique_lock lock(dbMutex);

        if (auto it = dependencies.find(from); it != dependencies.end())
        {
            it->second.erase(to);
            if (it->second.empty()) dependencies.erase(it);
        }

        if (auto it = dependents.find(to); it != dependents.end())
        {
            it->second.erase(from);
            if (it->second.empty()) dependents.erase(it);
        }
    }

    void AssetDatabase::clearDependencies(const AssetGUID& guid)
    {
        std::unique_lock lock(dbMutex);

        // Remove forward deps
        if (auto it = dependencies.find(guid); it != dependencies.end())
        {
            for (const auto& target : it->second)
            {
                if (auto revIt = dependents.find(target); revIt != dependents.end())
                {
                    revIt->second.erase(guid);
                    if (revIt->second.empty()) dependents.erase(revIt);
                }
            }
            dependencies.erase(it);
        }
    }

    void AssetDatabase::setDependencies(const AssetGUID& guid, const std::vector<AssetGUID>& deps)
    {
        // Clear existing forward deps first (without lock, clearDependencies acquires it)
        clearDependencies(guid);

        std::unique_lock lock(dbMutex);
        for (const auto& dep : deps)
        {
            dependencies[guid].insert(dep);
            dependents[dep].insert(guid);
        }
    }

    std::vector<AssetGUID> AssetDatabase::getDependencies(const AssetGUID& guid) const
    {
        std::shared_lock lock(dbMutex);
        auto it = dependencies.find(guid);
        if (it == dependencies.end()) return {};
        return {it->second.begin(), it->second.end()};
    }

    std::vector<AssetGUID> AssetDatabase::getDependents(const AssetGUID& guid) const
    {
        std::shared_lock lock(dbMutex);
        auto it = dependents.find(guid);
        if (it == dependents.end()) return {};
        return {it->second.begin(), it->second.end()};
    }

    bool AssetDatabase::saveIndex(const std::string& projectRoot) const
    {
        try
        {
            fs::path indexPath = fs::path(projectRoot) / "assetdb.json";

            json j;
            j["version"] = 1;

            json assetsObj = json::object();
            {
                std::shared_lock lock(dbMutex);
                for (const auto& [guid, entry] : guidToEntry)
                {
                    json assetObj;
                    assetObj["path"] = entry.path;
                    assetObj["type"] = resource::assetTypeName(entry.type);
                    if (!entry.importSource.empty())
                    {
                        assetObj["importSource"] = entry.importSource;
                    }
                    assetsObj[guid.toString()] = assetObj;
                }
            }
            j["assets"] = assetsObj;

            // Serialize dependency graph
            json depsObj = json::object();
            {
                std::shared_lock lock(dbMutex);
                for (const auto& [from, targets] : dependencies)
                {
                    json arr = json::array();
                    for (const auto& to : targets)
                    {
                        arr.push_back(to.toString());
                    }
                    depsObj[from.toString()] = arr;
                }
            }
            j["dependencies"] = depsObj;

            std::ofstream file(indexPath);
            if (!file.is_open())
            {
                vfLogError("Failed to open asset database index for writing: {}", indexPath.string());
                return false;
            }

            file << j.dump(2);
            vfLogInfo("Saved asset database index with {} assets", guidToEntry.size());
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save asset database index: {}", e.what());
            return false;
        }
    }

    bool AssetDatabase::loadIndex(const std::string& projectRoot)
    {
        try
        {
            fs::path indexPath = fs::path(projectRoot) / "assetdb.json";

            if (!fs::exists(indexPath))
            {
                return false;
            }

            std::ifstream file(indexPath);
            if (!file.is_open())
            {
                return false;
            }

            json j = json::parse(file);

            std::unique_lock lock(dbMutex);

            guidToEntry.clear();
            pathToGuid.clear();
            dependencies.clear();
            dependents.clear();

            if (j.contains("assets"))
            {
                for (auto& [guidStr, assetObj] : j["assets"].items())
                {
                    AssetGUID guid = AssetGUID::fromString(guidStr);
                    if (!guid.isValid()) continue;

                    AssetDatabaseEntry entry;
                    entry.guid = guid;
                    entry.path = normalizePath(assetObj.value("path", ""));
                    entry.type = AssetMetadataSerializer::stringToAssetType(assetObj.value("type", ""));
                    entry.importSource = assetObj.value("importSource", "");

                    guidToEntry[guid] = entry;
                    pathToGuid[entry.path] = guid;
                }
            }

            if (j.contains("dependencies"))
            {
                for (auto& [fromStr, targets] : j["dependencies"].items())
                {
                    AssetGUID from = AssetGUID::fromString(fromStr);
                    if (!from.isValid()) continue;

                    for (const auto& toStr : targets)
                    {
                        AssetGUID to = AssetGUID::fromString(toStr.get<std::string>());
                        if (!to.isValid()) continue;

                        dependencies[from].insert(to);
                        dependents[to].insert(from);
                    }
                }
            }

            vfLogInfo("Loaded asset database index with {} assets", guidToEntry.size());
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to load asset database index: {}", e.what());
            return false;
        }
    }

    bool AssetDatabase::rebuildFromMetaFiles(const std::string& searchRoot)
    {
        try
        {
            std::unique_lock lock(dbMutex);

            guidToEntry.clear();
            pathToGuid.clear();
            dependencies.clear();
            dependents.clear();

            lock.unlock();

            uint32_t count = 0;
            std::error_code ec;
            for (const auto& entry : fs::recursive_directory_iterator(
                     searchRoot, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec)
                {
                    ec.clear();
                    continue;
                }

                if (!entry.is_regular_file()) continue;

                std::string ext = entry.path().extension().string();
                if (ext != ".vfmeta") continue;

                auto metadata = AssetMetadataSerializer::load(entry.path());
                if (!metadata) continue;

                // Derive asset path by removing .vfmeta extension
                fs::path assetPath = entry.path();
                assetPath = assetPath.parent_path() / assetPath.stem();

                registerAssetWithGUID(metadata->guid, assetPath.string(),
                                      metadata->type, metadata->importSourcePath);
                count++;
            }

            vfLogInfo("Rebuilt asset database from {} meta files", count);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to rebuild asset database from meta files: {}", e.what());
            return false;
        }
    }

    void AssetDatabase::clear()
    {
        std::unique_lock lock(dbMutex);
        guidToEntry.clear();
        pathToGuid.clear();
        dependencies.clear();
        dependents.clear();
    }
}
