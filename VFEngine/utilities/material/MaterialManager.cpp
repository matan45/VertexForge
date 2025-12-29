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

        // Invalidate cache and reload fresh from disk
        resource::ResourceManager::invalidateMaterialCache(path);
        auto material = resource::ResourceManager::loadMaterial(path);
        if (!material) {
            vfLogError("Failed to reload material: {}", path);
            return false;
        }
        material->needsRecompile = true;

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

        // Invalidate cache and reload to update cached version
        resource::ResourceManager::invalidateMaterialCache(path);
        auto cached = resource::ResourceManager::loadMaterial(path);
        if (cached) {
            cached->needsRecompile = true;
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
