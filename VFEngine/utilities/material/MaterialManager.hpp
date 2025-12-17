#pragma once
#include "MaterialTypes.hpp"
#include "MaterialAsset.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <functional>
#include <vector>

namespace material {

    // Callback for material changes (for hot-reload support)
    using MaterialChangedCallback = std::function<void(const std::string& materialPath)>;

    /**
     * MaterialManager handles material editing operations:
     * - Saving/creating materials
     * - Reloading materials (hot-reload)
     * - Change notification callbacks
     *
     * For loading materials, use ResourceManager::loadMaterial() or loadMaterialAsync().
     */
    class MaterialManager {
    private:
        mutable std::mutex callbackMutex;

        // Strong reference to default material
        std::shared_ptr<MaterialData> defaultMaterial;

        std::vector<MaterialChangedCallback> changeCallbacks;

    public:
        static MaterialManager& instance();

        // Reload material from disk and update any cached instances
        bool reloadMaterial(std::string_view path);

        // Save material to disk
        bool saveMaterial(std::string_view path, const MaterialData& material);

        // Create a new material (optionally save to disk)
        std::shared_ptr<MaterialData> createMaterial(const std::string& name, std::string_view savePath = "");

        // Register callback for material changes (for hot-reload support)
        void registerChangeCallback(MaterialChangedCallback callback);
        void clearCallbacks();

        // Get default material (lazy-created)
        std::shared_ptr<MaterialData> getDefaultMaterial();

    private:
        MaterialManager() = default;
        ~MaterialManager() = default;
        MaterialManager(const MaterialManager&) = delete;
        MaterialManager& operator=(const MaterialManager&) = delete;

        void notifyMaterialChanged(const std::string& path);
    };

}
