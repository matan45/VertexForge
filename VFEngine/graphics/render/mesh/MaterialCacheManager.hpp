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

    class MaterialCacheManager
    {
    private:
        mutable std::unordered_map<std::string, std::shared_ptr<material::MaterialData>> materialCache;
        mutable std::shared_mutex cacheMutex;
        mutable bool cacheInvalidated = false;

        // Optional references to related caches for coordinated invalidation
        MaterialShaderCache* shaderCache = nullptr;
        MaterialTextureCache* textureCache = nullptr;

    public:
        MaterialCacheManager() = default;
        ~MaterialCacheManager() = default;

        // Set related caches for coordinated invalidation
        void setShaderCache(MaterialShaderCache* cache) { shaderCache = cache; }
        void setTextureCache(MaterialTextureCache* cache) { textureCache = cache; }

        // Get a material from cache, or load and cache it if not present
        std::shared_ptr<material::MaterialData> getMaterial(const std::string& materialPath) const;

        // Get a material from cache only (returns nullptr if not cached)
        std::shared_ptr<material::MaterialData> getCachedMaterial(const std::string& materialPath) const;

        // Invalidate cache - empty path invalidates all, specific path invalidates one
        void invalidate(const std::string& materialPath = "");

        // Inject a material into the cache (for preview with unsaved changes)
        void injectForPreview(const std::string& materialPath,
                              std::shared_ptr<material::MaterialData> materialData);

        // Check and clear invalidation flag (called at frame start)
        bool checkAndClearInvalidation();

        // Clear entire cache
        void clear();

        // Acquire locks for batch operations
        std::unique_lock<std::shared_mutex> acquireExclusiveLock() const { return std::unique_lock(cacheMutex); }
        std::shared_lock<std::shared_mutex> acquireSharedLock() const { return std::shared_lock(cacheMutex); }

        // Direct cache access (use with lock held)
        const std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& getCache() const { return materialCache; }
    };
}
