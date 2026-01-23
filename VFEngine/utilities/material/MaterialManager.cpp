#include "MaterialManager.hpp"
#include "MaterialAsset.hpp"
#include "MaterialInstanceAsset.hpp"
#include "../print/EditorLogger.hpp"
#include "../resource/ResourceManager.hpp"
#include <algorithm>

namespace material {

    MaterialManager& MaterialManager::instance() {
        static MaterialManager inst;
        return inst;
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

        // Notify all instances that use this material as parent
        notifyInstancesOfParentChange(pathStr);

        return true;
    }

    bool MaterialManager::saveMaterial(std::string_view path, const MaterialData& material) {
        if (!MaterialAsset::save(path, material)) {
            vfLogError("Failed to save material: {}", path);
            return false;
        }

        std::string pathStr(path);

        // Invalidate cache so other systems reload fresh data when needed.
        // Don't reload immediately - this avoids file system race conditions
        // where the read might get stale data before the write fully flushes.
        resource::ResourceManager::invalidateMaterialCache(path);

        notifyMaterialChanged(pathStr);

        // Notify all instances that use this material as parent
        notifyInstancesOfParentChange(pathStr);

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

    CallbackId MaterialManager::registerChangeCallback(MaterialChangedCallback callback) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        CallbackId id{nextCallbackIdValue++};
        changeCallbacks[id] = std::move(callback);
        return id;
    }

    void MaterialManager::unregisterChangeCallback(CallbackId id) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        changeCallbacks.erase(id);
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
        std::unordered_map<CallbackId, MaterialChangedCallback> callbacksCopy;
        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            callbacksCopy = changeCallbacks;
        }

        for (const auto& [id, callback] : callbacksCopy) {
            callback(path);
        }
    }

    std::shared_ptr<MaterialInstanceData> MaterialManager::createInstance(
        const std::string& name,
        const std::string& parentPath,
        std::string_view savePath)
    {
        // Validate parent exists
        auto parentMaterial = resource::ResourceManager::loadMaterial(parentPath);
        if (!parentMaterial) {
            vfLogError("Cannot create instance: parent material not found: {}", parentPath);
            return nullptr;
        }

        auto instance = std::make_shared<MaterialInstanceData>(
            MaterialInstanceAsset::createDefault(name, parentPath));

        if (!savePath.empty()) {
            if (!MaterialInstanceAsset::save(savePath, *instance)) {
                vfLogError("Failed to save new material instance: {}", savePath);
                return nullptr;
            }

            // Register the instance relationship
            registerInstance(std::string(savePath), parentPath);
        }

        return instance;
    }

    bool MaterialManager::saveInstance(std::string_view path, const MaterialInstanceData& instance) {
        if (!MaterialInstanceAsset::save(path, instance)) {
            vfLogError("Failed to save material instance: {}", path);
            return false;
        }

        std::string pathStr(path);

        // Invalidate cache so other systems reload fresh data when needed.
        // Don't reload immediately - this avoids file system race conditions.
        resource::ResourceManager::invalidateMaterialInstanceCache(path);

        // Register/update instance relationship
        registerInstance(pathStr, instance.parentMaterialPath);

        notifyMaterialChanged(pathStr);

        return true;
    }

    bool MaterialManager::reloadInstance(std::string_view path) {
        std::string pathStr(path);

        resource::ResourceManager::invalidateMaterialInstanceCache(path);
        auto instance = resource::ResourceManager::loadMaterialInstance(path);
        if (!instance) {
            vfLogError("Failed to reload material instance: {}", path);
            return false;
        }

        // Update instance relationship (in case parent changed)
        registerInstance(pathStr, instance->parentMaterialPath);

        notifyMaterialChanged(pathStr);

        return true;
    }

    // Instance relationship tracking

    void MaterialManager::registerInstance(const std::string& instancePath, const std::string& parentPath) {
        std::lock_guard<std::mutex> lock(callbackMutex);

        // Remove old parent mapping if exists
        auto oldIt = instanceToParent.find(instancePath);
        if (oldIt != instanceToParent.end()) {
            const std::string& oldParent = oldIt->second;
            auto& instances = parentToInstances[oldParent];
            instances.erase(std::remove(instances.begin(), instances.end(), instancePath), instances.end());
            if (instances.empty()) {
                parentToInstances.erase(oldParent);
            }
        }

        // Add new mapping
        instanceToParent[instancePath] = parentPath;
        parentToInstances[parentPath].push_back(instancePath);
    }

    void MaterialManager::unregisterInstance(const std::string& instancePath) {
        std::lock_guard<std::mutex> lock(callbackMutex);

        auto it = instanceToParent.find(instancePath);
        if (it == instanceToParent.end()) {
            return;
        }

        const std::string& parentPath = it->second;
        auto& instances = parentToInstances[parentPath];
        instances.erase(std::remove(instances.begin(), instances.end(), instancePath), instances.end());
        if (instances.empty()) {
            parentToInstances.erase(parentPath);
        }

        instanceToParent.erase(it);
    }

    std::vector<std::string> MaterialManager::getInstancesOfParent(const std::string& parentPath) const {
        std::lock_guard<std::mutex> lock(callbackMutex);

        auto it = parentToInstances.find(parentPath);
        if (it != parentToInstances.end()) {
            return it->second;
        }
        return {};
    }

    std::string MaterialManager::getParentOfInstance(const std::string& instancePath) const {
        std::lock_guard<std::mutex> lock(callbackMutex);

        auto it = instanceToParent.find(instancePath);
        if (it != instanceToParent.end()) {
            return it->second;
        }
        return "";
    }

    void MaterialManager::notifyInstancesOfParentChange(const std::string& parentPath) {
        std::vector<std::string> instances;
        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            auto it = parentToInstances.find(parentPath);
            if (it != parentToInstances.end()) {
                instances = it->second;
            }
        }

        // Invalidate cache and notify for each instance
        for (const auto& instancePath : instances) {
            resource::ResourceManager::invalidateMaterialInstanceCache(instancePath);
            notifyMaterialChanged(instancePath);
        }
    }

}
