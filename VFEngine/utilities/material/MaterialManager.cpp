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
        
        resource::ResourceManager::invalidateMaterialCache(path);
        auto material = resource::ResourceManager::loadMaterial(path);
        if (!material) {
            vfLogError("Failed to reload material: {}", path);
            return false;
        }
        material->needsRecompile = true;
        
        notifyMaterialChanged(pathStr);

        return true;
    }

    bool MaterialManager::saveMaterial(std::string_view path, const MaterialData& material) {
        if (!MaterialAsset::save(path, material)) {
            vfLogError("Failed to save material: {}", path);
            return false;
        }

        std::string pathStr(path);
        
        resource::ResourceManager::invalidateMaterialCache(path);
        auto cached = resource::ResourceManager::loadMaterial(path);
        if (cached) {
            cached->needsRecompile = true;
        }
        
        notifyMaterialChanged(pathStr);

        return true;
    }

    std::shared_ptr<MaterialData> MaterialManager::createMaterial(const std::string& name, std::string_view savePath) {
        auto material = std::make_shared<MaterialData>(MaterialAsset::createDefault(name));
        
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
