#include "MaterialManager.hpp"
#include "MaterialAsset.hpp"
#include "../print/EditorLogger.hpp"
#include "../resource/ResourceManager.hpp"

namespace material {

    MaterialManager& MaterialManager::instance() {
        static MaterialManager instance;
        return instance;
    }

    bool MaterialManager::reloadMaterial(std::string_view path) {
        std::string pathStr(path);

        // Load fresh data from disk
        auto newData = MaterialAsset::load(path);
        if (!newData) {
            vfLogError("Failed to reload material: {}", path);
            return false;
        }

        // Try to update existing cached material in-place
        auto existing = resource::ResourceManager::getMaterial(path);
        if (existing) {
            *existing = std::move(*newData);
            existing->needsRecompile = true;
        } else {
            // Not in cache, invalidate and let next load get fresh data
            resource::ResourceManager::invalidateMaterialCache(path);
        }

        // Notify callbacks
        notifyMaterialChanged(pathStr);

        return true;
    }

    bool MaterialManager::saveMaterial(std::string_view path, const MaterialData& material) {
        // Save to disk
        if (!MaterialAsset::save(path, material)) {
            vfLogError("Failed to save material: {}", path);
            return false;
        }

        std::string pathStr(path);

        // Update cached material if it exists
        auto existing = resource::ResourceManager::getMaterial(path);
        if (existing) {
            *existing = material;
            existing->needsRecompile = true;
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
        }

        return material;
    }

    void MaterialManager::registerChangeCallback(MaterialChangedCallback callback) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        changeCallbacks.push_back(std::move(callback));
    }

    void MaterialManager::clearCallbacks() {
        std::lock_guard<std::mutex> lock(callbackMutex);
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
            std::lock_guard<std::mutex> lock(callbackMutex);
            callbacks = changeCallbacks;
        }

        for (const auto& callback : callbacks) {
            callback(path);
        }
    }

}
