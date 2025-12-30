#pragma once

#include "IBLTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <array>

namespace core
{
    class Device;
}

namespace render::ibl
{
    class DefaultIBLTextureFactory
    {
    private:
        core::Device& device;

        ImageData irradiance{};
        ImageData prefilter{};
        ImageData brdfLUT{};

        bool texturesCreated = false;

        // Default IBL colors - uniform across all cubemap faces
        inline static constexpr std::array<std::array<float, 4>, 6> studioIrradiance = {
            {
                {0.8f, 0.8f, 0.85f, 1.0f},
                {0.8f, 0.8f, 0.85f, 1.0f},
                {0.8f, 0.8f, 0.85f, 1.0f},
                {0.8f, 0.8f, 0.85f, 1.0f},
                {0.8f, 0.8f, 0.85f, 1.0f},
                {0.8f, 0.8f, 0.85f, 1.0f}
            }
        };

        inline static constexpr std::array<std::array<float, 4>, 6> studioPrefilter = {
            {
                {0.6f, 0.6f, 0.65f, 1.0f},
                {0.6f, 0.6f, 0.65f, 1.0f},
                {0.6f, 0.6f, 0.65f, 1.0f},
                {0.6f, 0.6f, 0.65f, 1.0f},
                {0.6f, 0.6f, 0.65f, 1.0f},
                {0.6f, 0.6f, 0.65f, 1.0f}
            }
        };

        // Default BRDF LUT: (scale=1.0, bias=0.0)
        inline static constexpr std::array<float, 4> defaultBrdfPixel = {1.0f, 0.0f, 0.0f, 1.0f};

    public:
        explicit DefaultIBLTextureFactory(core::Device& device);
        ~DefaultIBLTextureFactory();

        void createDefaultTextures(vk::CommandPool commandPool);

        void cleanup();

        const ImageData& getIrradiance() const { return irradiance; }
        const ImageData& getPrefilter() const { return prefilter; }
        const ImageData& getBrdfLUT() const { return brdfLUT; }

        bool isCreated() const { return texturesCreated; }

    private:
        void createCubemap(
            vk::CommandPool commandPool,
            ImageData& imageData,
            const std::array<std::array<float, 4>, 6>& faceColors);

        void create2DTexture(
            vk::CommandPool commandPool,
            ImageData& imageData);
    };
}
