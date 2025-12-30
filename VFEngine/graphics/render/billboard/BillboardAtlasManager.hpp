#pragma once

#include "BillboardTypes.hpp"
#include <memory>
#include <string>

namespace core
{
    class Device;
    class Texture;
}

namespace render::billboard
{
    class BillboardAtlasManager
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Texture> atlasTexture;

        // Default atlas (magenta placeholder)
        vk::Image defaultAtlasImage;
        vk::DeviceMemory defaultAtlasImageMemory;
        vk::ImageView defaultAtlasImageView;
        vk::Sampler defaultAtlasSampler;

        bool atlasLoaded = false;

    public:
        explicit BillboardAtlasManager(core::Device& device);
        ~BillboardAtlasManager();

        void init();
        void cleanUp();

        bool loadAtlas(const std::string& atlasPath);

        // Get current atlas (loaded or default)
        vk::ImageView getImageView() const;
        vk::Sampler getSampler() const;
        bool isAtlasLoaded() const { return atlasLoaded; }

    private:
        void createDefaultAtlas();
    };
}
