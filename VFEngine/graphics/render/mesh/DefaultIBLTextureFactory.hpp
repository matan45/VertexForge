#pragma once

#include "../ibl/IBLTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <array>

namespace core
{
    class Device;
}

namespace render::mesh
{
    // Factory class for creating default/fallback IBL textures
    // Used when no environment map is loaded to provide basic PBR lighting
    class DefaultIBLTextureFactory
    {
    public:
        explicit DefaultIBLTextureFactory(core::Device& device);
        ~DefaultIBLTextureFactory();

        // Create all default IBL textures (irradiance, prefilter, BRDF LUT)
        void createDefaultTextures(vk::CommandPool commandPool);

        // Cleanup all created textures
        void cleanup();

        // Accessors for the created textures
        const ibl::ImageData& getIrradiance() const { return irradiance; }
        const ibl::ImageData& getPrefilter() const { return prefilter; }
        const ibl::ImageData& getBrdfLUT() const { return brdfLUT; }

        bool isCreated() const { return texturesCreated; }

    private:
        core::Device& device;

        ibl::ImageData irradiance{};
        ibl::ImageData prefilter{};
        ibl::ImageData brdfLUT{};

        bool texturesCreated = false;

        // Create a cubemap with specified face colors
        void createCubemap(
            vk::CommandPool commandPool,
            ibl::ImageData& imageData,
            const std::array<std::array<float, 4>, 6>& faceColors);

        // Create a 2D texture for BRDF LUT
        void create2DTexture(
            vk::CommandPool commandPool,
            ibl::ImageData& imageData);
    };
}
