#include "MaterialManager.hpp"
#include "MaterialAsset.hpp"
#include "../print/EditorLogger.hpp"
#include <algorithm>

namespace material {

    MaterialManager& MaterialManager::instance() {
        static MaterialManager instance;
        return instance;
    }

    std::shared_ptr<MaterialData> MaterialManager::loadMaterial(std::string_view path) {
        std::string pathStr(path);

        std::lock_guard<std::mutex> lock(cacheMutex);

        // Check cache first
        auto it = materialCache.find(pathStr);
        if (it != materialCache.end()) {
            // Try to get existing material from weak_ptr
            if (auto existing = it->second.lock()) {
                return existing;
            }
            // Weak pointer expired, remove from cache
            materialCache.erase(it);
        }

        // Load from disk
        auto materialData = MaterialAsset::load(path);
        if (!materialData) {
            vfLogError("Failed to load material: {}", path);
            return nullptr;
        }

        // Create shared_ptr and cache weak_ptr
        auto material = std::make_shared<MaterialData>(std::move(*materialData));
        materialCache[pathStr] = material;

        return material;
    }

    std::future<std::shared_ptr<MaterialData>> MaterialManager::loadMaterialAsync(std::string_view path) {
        std::string pathStr(path);

        // Check cache first (with lock)
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            auto it = materialCache.find(pathStr);
            if (it != materialCache.end()) {
                if (auto existing = it->second.lock()) {
                    return makeReadyFuture(existing);
                }
                materialCache.erase(it);
            }
        }

        // Load asynchronously
        return std::async(std::launch::async, [this, pathStr]() {
            return loadMaterial(pathStr);
        });
    }

    std::shared_ptr<MaterialData> MaterialManager::getMaterial(std::string_view path) const {
        std::string pathStr(path);

        std::lock_guard<std::mutex> lock(cacheMutex);

        auto it = materialCache.find(pathStr);
        if (it != materialCache.end()) {
            return it->second.lock();
        }
        return nullptr;
    }

    bool MaterialManager::isMaterialLoaded(std::string_view path) const {
        std::string pathStr(path);

        std::lock_guard<std::mutex> lock(cacheMutex);

        auto it = materialCache.find(pathStr);
        if (it != materialCache.end()) {
            return !it->second.expired();
        }
        return false;
    }

    bool MaterialManager::reloadMaterial(std::string_view path) {
        std::string pathStr(path);

        // Load fresh data from disk
        auto newData = MaterialAsset::load(path);
        if (!newData) {
            vfLogError("Failed to reload material: {}", path);
            return false;
        }

        bool shouldNotify = false;
        {
            std::lock_guard<std::mutex> lock(cacheMutex);

            auto it = materialCache.find(pathStr);
            if (it != materialCache.end()) {
                if (auto existing = it->second.lock()) {
                    // Update existing material in place
                    *existing = std::move(*newData);
                    existing->needsRecompile = true;
                    shouldNotify = true;
                }
            }

            if (!shouldNotify) {
                // Material wasn't loaded, just load it fresh
                auto material = std::make_shared<MaterialData>(std::move(*newData));
                materialCache[pathStr] = material;
            }
        }

        // Notify callbacks outside lock to avoid deadlock
        if (shouldNotify) {
            notifyMaterialChanged(pathStr);
        }

        return true;
    }

    void MaterialManager::invalidateCache(std::string_view path) {
        std::string pathStr(path);

        std::lock_guard<std::mutex> lock(cacheMutex);
        materialCache.erase(pathStr);
    }

    bool MaterialManager::saveMaterial(std::string_view path, const MaterialData& material) {
        // Save to disk
        if (!MaterialAsset::save(path, material)) {
            vfLogError("Failed to save material: {}", path);
            return false;
        }

        std::string pathStr(path);

        // Update cache
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            auto it = materialCache.find(pathStr);
            if (it != materialCache.end()) {
                if (auto existing = it->second.lock()) {
                    *existing = material;
                    existing->needsRecompile = true;
                }
            }
        }

        // Notify callbacks
        notifyMaterialChanged(pathStr);

        return true;
    }

    std::shared_ptr<MaterialData> MaterialManager::createMaterial(const std::string& name, std::string_view savePath) {
        // Create default material
        auto material = std::make_shared<MaterialData>(MaterialAsset::createDefault(name));

        // Save if path provided
        if (!savePath.empty()) {
            if (!MaterialAsset::save(savePath, *material)) {
                vfLogError("Failed to save new material: {}", savePath);
                return nullptr;
            }

            // Cache it
            std::string pathStr(savePath);
            std::lock_guard<std::mutex> lock(cacheMutex);
            materialCache[pathStr] = material;
        }

        return material;
    }

    void MaterialManager::unloadUnusedMaterials() {
        std::lock_guard<std::mutex> lock(cacheMutex);

        // Remove expired weak_ptrs
        for (auto it = materialCache.begin(); it != materialCache.end();) {
            if (it->second.expired()) {
                it = materialCache.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::vector<std::string> MaterialManager::getLoadedMaterialPaths() const {
        std::lock_guard<std::mutex> lock(cacheMutex);

        std::vector<std::string> paths;
        paths.reserve(materialCache.size());

        for (const auto& [path, weakPtr] : materialCache) {
            if (!weakPtr.expired()) {
                paths.push_back(path);
            }
        }

        return paths;
    }

    void MaterialManager::registerChangeCallback(MaterialChangedCallback callback) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        changeCallbacks.push_back(std::move(callback));
    }

    void MaterialManager::clearCallbacks() {
        std::lock_guard<std::mutex> lock(cacheMutex);
        changeCallbacks.clear();
    }

    std::shared_ptr<MaterialData> MaterialManager::getDefaultMaterial() {
        if (!defaultMaterial) {
            defaultMaterial = std::make_shared<MaterialData>(MaterialAsset::createDefault("Default"));
        }
        return defaultMaterial;
    }

    void MaterialManager::notifyMaterialChanged(const std::string& path) {
        std::vector<MaterialChangedCallback> callbacks;
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            callbacks = changeCallbacks;
        }

        for (const auto& callback : callbacks) {
            callback(path);
        }
    }

}
