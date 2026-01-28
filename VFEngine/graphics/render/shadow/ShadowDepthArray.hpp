#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>

namespace core
{
    class Device;
}

namespace render::shadow
{
    class ShadowDepthArray
    {
    private:
        core::Device& device;

        vk::Image image;
        vk::DeviceMemory memory;
        vk::ImageView arrayView;
        std::vector<vk::ImageView> layerViews;

        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t layerCount = 0;
        vk::Format format = vk::Format::eD32Sfloat;

        bool initialized = false;

    public:
        explicit ShadowDepthArray(core::Device& device);
        ~ShadowDepthArray();

        ShadowDepthArray(const ShadowDepthArray&) = delete;
        ShadowDepthArray& operator=(const ShadowDepthArray&) = delete;
        ShadowDepthArray(ShadowDepthArray&&) noexcept;
        ShadowDepthArray& operator=(ShadowDepthArray&&) noexcept;

        void init(uint32_t width, uint32_t height, uint32_t layers,
                  vk::Format format = vk::Format::eD32Sfloat);
        void cleanup();

        [[nodiscard]] vk::Image getImage() const { return image; }
        [[nodiscard]] vk::ImageView getArrayView() const { return arrayView; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

        struct ExtractedResources
        {
            vk::Image image;
            vk::DeviceMemory memory;
            vk::ImageView arrayView;
            std::vector<vk::ImageView> layerViews;
        };
        [[nodiscard]] ExtractedResources extractResources();

    private:
        void createImage();
        void createImageViews();
    };
}
