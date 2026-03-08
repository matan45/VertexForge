#include "ImposterAtlasManager.hpp"

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

        // Stub: actual texture loading will be wired with BindlessTextureManager
        LoadedAtlas atlas{};
        atlas.bindlessIndex = 0;
        loadedAtlases[path] = atlas;
        return atlas.bindlessIndex;
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
