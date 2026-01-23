#pragma once
#include "MaterialTypes.hpp"
#include "MaterialInstanceTypes.hpp"
#include "MaterialInstanceAsset.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <functional>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace material
{
    // Callback for material changes (for hot-reload support)
    using MaterialChangedCallback = std::function<void(const std::string& materialPath)>;
    using CallbackId = uint64_t;

    class MaterialManager
    {
    private:
        mutable std::mutex callbackMutex;

        std::shared_ptr<MaterialData> defaultMaterial;

        std::unordered_map<CallbackId, MaterialChangedCallback> changeCallbacks;
        CallbackId nextCallbackId = 1;

        // Instance tracking: parent path -> list of instance paths
        std::unordered_map<std::string, std::vector<std::string>> parentToInstances;
        // Reverse mapping: instance path -> parent path
        std::unordered_map<std::string, std::string> instanceToParent;

    public:
        static MaterialManager& instance();

        // Material operations
        bool reloadMaterial(std::string_view path);
        bool saveMaterial(std::string_view path, const MaterialData& material);
        std::shared_ptr<MaterialData> createMaterial(const std::string& name, std::string_view savePath = "");
        std::shared_ptr<MaterialData> getDefaultMaterial();

        // Instance operations
        std::shared_ptr<MaterialInstanceData> createInstance(
            const std::string& name,
            const std::string& parentPath,
            std::string_view savePath = "");
        bool saveInstance(std::string_view path, const MaterialInstanceData& instance);
        bool reloadInstance(std::string_view path);

        // Instance relationship tracking
        void registerInstance(const std::string& instancePath, const std::string& parentPath);
        void unregisterInstance(const std::string& instancePath);
        std::vector<std::string> getInstancesOfParent(const std::string& parentPath) const;
        std::string getParentOfInstance(const std::string& instancePath) const;

        // Callbacks
        CallbackId registerChangeCallback(MaterialChangedCallback callback);
        void unregisterChangeCallback(CallbackId id);
        void clearCallbacks();

    private:
        explicit MaterialManager() = default;
        ~MaterialManager() = default;
        MaterialManager(const MaterialManager&) = delete;
        MaterialManager& operator=(const MaterialManager&) = delete;

        void notifyMaterialChanged(const std::string& path);
        void notifyInstancesOfParentChange(const std::string& parentPath);
    };
}
