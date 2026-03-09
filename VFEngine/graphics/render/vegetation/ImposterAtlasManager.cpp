#include "ImposterAtlasManager.hpp"
#include "print/Log.hpp"

namespace render::vegetation
{
    void ImposterAtlasManager::init(core::Device& device)
    {
        devicePtr = &device;
    }

    void ImposterAtlasManager::cleanup()
    {
        loadedAtlases.clear();
        devicePtr = nullptr;
    }

    uint32_t ImposterAtlasManager::loadAtlas(const std::string& path)
    {
        auto it = loadedAtlases.find(path);
        if (it != loadedAtlases.end())
        {
            return it->second.bindlessIndex;
        }

        // Note: Imposter atlas loading is handled by GPUDrivenRenderer::loadImposterAtlas()
        // which creates the GPU texture and registers with BindlessTextureManager directly.
        // This class serves as a cache lookup only. Use GPUDrivenRenderer for actual loading.
        vfLogWarning("ImposterAtlasManager::loadAtlas called but loading is handled by GPUDrivenRenderer. "
                     "Use GPUDrivenRenderer::loadImposterAtlas() instead for: {}", path);
        return 0;
    }

    void ImposterAtlasManager::unloadAtlas(const std::string& path)
    {
        loadedAtlases.erase(path);
    }

    bool ImposterAtlasManager::hasAtlas(const std::string& path) const
    {
        return loadedAtlases.contains(path);
    }

    uint32_t ImposterAtlasManager::getAtlasTextureIndex(const std::string& path) const
    {
        auto it = loadedAtlases.find(path);
        if (it != loadedAtlases.end())
        {
            return it->second.bindlessIndex;
        }
        return 0;
    }
}
