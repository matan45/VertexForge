#pragma once

#include "material/MaterialTypes.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <shared_mutex>

namespace render::mesh
{
    class MaterialShaderCache;
    class MaterialTextureCache;
    class MaterialParameterBufferCache;

    class MaterialCacheManager
    {
    private:
        mutable std::unordered_map<std::string, std::shared_ptr<material::MaterialData>> materialCache;
        mutable std::shared_mutex cacheMutex;
        mutable bool cacheInvalidated = false;

        // Optional references to related caches for coordinated invalidation
        MaterialShaderCache* shaderCache = nullptr;
        MaterialTextureCache* textureCache = nullptr;
        MaterialParameterBufferCache* parameterBufferCache = nullptr;

    public:
        explicit MaterialCacheManager() = default;
        ~MaterialCacheManager() = default;

        void setShaderCache(MaterialShaderCache* cache) { shaderCache = cache; }
        void setTextureCache(MaterialTextureCache* cache) { textureCache = cache; }
        void setParameterBufferCache(MaterialParameterBufferCache* cache) { parameterBufferCache = cache; }
        
        std::shared_ptr<material::MaterialData> getMaterial(const std::string& materialPath) const;

        void invalidate(const std::string& materialPath = "");
        
        void injectForPreview(const std::string& materialPath,
                              std::shared_ptr<material::MaterialData> materialData);
        
        bool checkAndClearInvalidation();
        
        void clear();
        
        std::shared_lock<std::shared_mutex> acquireSharedLock() const { return std::shared_lock(cacheMutex); }
        
        const std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& getCache() const
        {
            return materialCache;
        }
    };
}
