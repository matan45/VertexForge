#include "MaterialCacheManager.hpp"
#include "../material/MaterialShaderCache.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "resource/ResourceManager.hpp"
#include "asset/AssetRef.hpp"
#include <material/MaterialInstanceTypes.hpp>

namespace render::mesh
{
    std::shared_ptr<material::MaterialData> MaterialCacheManager::getMaterial(const std::string& materialPath) const
    {
        if (materialPath.empty())
        {
            return nullptr;
        }

        // For material instances, we need to load the parent material
        std::string effectivePath = materialPath;
        if (material::isInstanceFile(materialPath))
        {
            auto instanceData = resource::ResourceManager::loadMaterialInstance(asset::AssetRef::fromPath(materialPath));
            if (!instanceData || instanceData->parentMaterialRef.resolve().empty())
            {
                return nullptr;
            }
            effectivePath = instanceData->parentMaterialRef.resolve();
        }

        // First check cache with shared lock
        {
            std::shared_lock lock(cacheMutex);
            auto it = materialCache.find(effectivePath);
            if (it != materialCache.end() && it->second)
            {
                return it->second;
            }
        }

        // Not in cache, load with exclusive lock
        std::unique_lock lock(cacheMutex);

        // Double-check after acquiring exclusive lock
        auto it = materialCache.find(effectivePath);
        if (it != materialCache.end() && it->second)
        {
            return it->second;
        }

        auto matData = resource::ResourceManager::loadMaterial(asset::AssetRef::fromPath(effectivePath));
        if (matData)
        {
            materialCache[effectivePath] = matData;
        }
        return matData;
    }

    void MaterialCacheManager::invalidate(const std::string& materialPath)
    {
        std::unique_lock lock(cacheMutex);

        if (materialPath.empty())
        {
            // Mark cache for full invalidation (deferred until next frame)
            cacheInvalidated = true;

            // Also invalidate all compiled shaders
            if (shaderCache)
            {
                shaderCache->invalidateAll();
            }
        }
        else
        {
            materialCache.erase(materialPath);

            // Also invalidate the compiled shader for this material
            if (shaderCache)
            {
                shaderCache->invalidate(materialPath);
            }

            // Invalidate the per-material texture descriptor set
            if (textureCache)
            {
                textureCache->invalidateMaterialDescriptorSet(materialPath);
            }
        }
    }

    void MaterialCacheManager::injectForPreview(const std::string& materialPath,
                                                std::shared_ptr<material::MaterialData> materialData)
    {
        if (materialPath.empty() || !materialData)
        {
            return;
        }

        {
            std::unique_lock lock(cacheMutex);
            materialCache[materialPath] = materialData;
        }

        // If the material has custom shaders, invalidate the shader cache to force recompilation
        if (!materialData->cachedVertexShader.empty() && !materialData->cachedFragmentShader.empty())
        {
            if (shaderCache)
            {
                shaderCache->invalidate(materialPath);
            }
        }
    }

    bool MaterialCacheManager::checkAndClearInvalidation()
    {
        std::unique_lock lock(cacheMutex);
        if (cacheInvalidated)
        {
            materialCache.clear();
            cacheInvalidated = false;
            return true;
        }
        return false;
    }

    void MaterialCacheManager::clear()
    {
        std::unique_lock lock(cacheMutex);
        materialCache.clear();
        cacheInvalidated = false;
    }
}
