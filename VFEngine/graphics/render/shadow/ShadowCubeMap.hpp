#pragma once

#include "ShadowTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <array>

namespace core
{
    class Device;
}

namespace render::shadow
{
    class ShadowCubeMap
    {
    private:
        core::Device& device;

        vk::Image image;
        vk::DeviceMemory memory;
        vk::ImageView cubeView;
        std::array<vk::ImageView, ShadowConstants::CUBE_FACE_COUNT> faceViews;

        std::array<vk::Framebuffer, ShadowConstants::CUBE_FACE_COUNT> cachedFramebuffers{};
        vk::RenderPass cachedRenderPass{};

        uint32_t size = 0;
        vk::Format format = vk::Format::eD32Sfloat;

        bool initialized = false;
        vk::ImageLayout currentLayout = vk::ImageLayout::eUndefined;

    public:
        explicit ShadowCubeMap(core::Device& device);
        ~ShadowCubeMap();

        ShadowCubeMap(const ShadowCubeMap&) = delete;
        ShadowCubeMap& operator=(const ShadowCubeMap&) = delete;
        ShadowCubeMap(ShadowCubeMap&&) noexcept;
        ShadowCubeMap& operator=(ShadowCubeMap&&) noexcept;

        void init(uint32_t size, vk::Format format = vk::Format::eD32Sfloat);
        void cleanup();

        [[nodiscard]] vk::ImageView getCubeView() const { return cubeView; }
        [[nodiscard]] vk::Image getImage() const { return image; }
        [[nodiscard]] uint32_t getSize() const { return size; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

        void transitionToDepthAttachment(vk::CommandBuffer cmd);
        void transitionToShaderRead(vk::CommandBuffer cmd);

        [[nodiscard]] vk::Framebuffer getOrCreateFramebuffer(uint32_t face, vk::RenderPass renderPass,
                                                              const vk::Device& logicalDevice);

        struct ExtractedResources
        {
            vk::Image image;
            vk::DeviceMemory memory;
            vk::ImageView cubeView;
            std::array<vk::ImageView, ShadowConstants::CUBE_FACE_COUNT> faceViews;
            std::array<vk::Framebuffer, ShadowConstants::CUBE_FACE_COUNT> framebuffers;
        };
        [[nodiscard]] ExtractedResources extractResources();

    private:
        void createImage();
        void createImageViews();
        void transitionFaces(vk::CommandBuffer cmd, uint32_t baseFace, uint32_t count,
                             vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
    };
}
