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
    
    class MaterialManager {
    private:
        mutable std::mutex callbackMutex;
        
        std::shared_ptr<MaterialData> defaultMaterial;

        std::vector<MaterialChangedCallback> changeCallbacks;

    public:
        static MaterialManager& instance();
        
        bool reloadMaterial(std::string_view path);
        
        bool saveMaterial(std::string_view path, const MaterialData& material);
        
        std::shared_ptr<MaterialData> createMaterial(const std::string& name, std::string_view savePath = "");
        
        void registerChangeCallback(MaterialChangedCallback callback);
        void clearCallbacks();
        
        std::shared_ptr<MaterialData> getDefaultMaterial();

    private:
        explicit MaterialManager() = default;
        ~MaterialManager() = default;
        MaterialManager(const MaterialManager&) = delete;
        MaterialManager& operator=(const MaterialManager&) = delete;

        void notifyMaterialChanged(const std::string& path);
    };

}
