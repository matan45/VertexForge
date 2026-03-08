#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

namespace core { class Device; }

namespace render::vegetation
{
    class ImposterAtlasManager
    {
    public:
        void init(core::Device& device);
        void cleanup();

        uint32_t loadAtlas(const std::string& path);
        void unloadAtlas(const std::string& path);

        [[nodiscard]] bool hasAtlas(const std::string& path) const;
        [[nodiscard]] uint32_t getAtlasTextureIndex(const std::string& path) const;

    private:
        struct LoadedAtlas
        {
            uint32_t bindlessIndex = 0;
            uint32_t width = 0;
            uint32_t height = 0;
        };

        std::unordered_map<std::string, LoadedAtlas> loadedAtlases;
        core::Device* devicePtr = nullptr;
    };
}
