#pragma once
#include "MaterialTypes.hpp"
#include "MaterialAsset.hpp"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <future>
#include <string>
#include <functional>

namespace material {

    // Callback for material changes (for hot-reload support)
    using MaterialChangedCallback = std::function<void(const std::string& materialPath)>;

    class MaterialManager {
    public:
        // Get singleton instance
        static MaterialManager& instance();

        // Load material (cached)
        std::shared_ptr<MaterialData> loadMaterial(std::string_view path);

        // Load material async
        std::future<std::shared_ptr<MaterialData>> loadMaterialAsync(std::string_view path);

        // Get cached material (returns nullptr if not loaded)
        std::shared_ptr<MaterialData> getMaterial(std::string_view path) const;

        // Check if material is loaded
        bool isMaterialLoaded(std::string_view path) const;

        // Reload a material from disk (for hot-reload)
        bool reloadMaterial(std::string_view path);

        // Save material to disk and update cache
        bool saveMaterial(std::string_view path, const MaterialData& material);

        // Create and cache a new default material
        std::shared_ptr<MaterialData> createMaterial(const std::string& name, std::string_view savePath = "");

        // Unload unused materials (weak_ptr cleanup)
        void unloadUnusedMaterials();

        // Get all loaded material paths
        std::vector<std::string> getLoadedMaterialPaths() const;

        // Register callback for material changes
        void registerChangeCallback(MaterialChangedCallback callback);

        // Clear all callbacks
        void clearCallbacks();

        // Get or create default material
        std::shared_ptr<MaterialData> getDefaultMaterial();

    private:
        MaterialManager() = default;
        ~MaterialManager() = default;
        MaterialManager(const MaterialManager&) = delete;
        MaterialManager& operator=(const MaterialManager&) = delete;

        // Material cache (weak_ptr allows auto-cleanup when no longer used)
        mutable std::mutex cacheMutex;
        std::unordered_map<std::string, std::weak_ptr<MaterialData>> materialCache;

        // Strong references to keep frequently used materials in memory
        std::shared_ptr<MaterialData> defaultMaterial;

        // Change callbacks
        std::vector<MaterialChangedCallback> changeCallbacks;

        // Notify callbacks of material change
        void notifyMaterialChanged(const std::string& path);

        // Helper to make ready future
        template<typename T>
        static std::future<T> makeReadyFuture(T value) {
            std::promise<T> promise;
            promise.set_value(std::move(value));
            return promise.get_future();
        }
    };

}
