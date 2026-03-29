#pragma once

#include "../../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>

namespace core
{
    class Device;
    struct OffscreenResources;
}

namespace render::vfx
{
    class DistortionResources
    {
    public:
        explicit DistortionResources(core::Device& device);
        ~DistortionResources();

        DistortionResources(const DistortionResources&) = delete;
        DistortionResources& operator=(const DistortionResources&) = delete;

        void init(vk::Format colorFormat, vk::Format depthFormat, vk::Extent2D swapExtent,
                  vk::ImageView sceneDepthView, const core::OffscreenResources& offscreen);
        void recreate(vk::Format colorFormat, vk::Format depthFormat, vk::Extent2D swapExtent,
                      vk::ImageView sceneDepthView, const core::OffscreenResources& offscreen);
        void cleanup();

        void copySceneColor(vk::CommandBuffer cmd, vk::Image srcColorImage, uint32_t width, uint32_t height);

        [[nodiscard]] vk::RenderPass getDistortionVectorRenderPass() const { return distortionVectorRenderPass; }
        [[nodiscard]] vk::RenderPass getCompositeRenderPass() const { return compositeRenderPass; }
        [[nodiscard]] vk::Framebuffer getDistortionVectorFramebuffer() const { return distortionVectorFramebuffer; }
        [[nodiscard]] vk::DescriptorSetLayout getCompositeDescriptorSetLayout() const { return compositeDescriptorSetLayout; }
        [[nodiscard]] vk::DescriptorSet getCompositeDescriptorSet() const { return compositeDescriptorSet; }
        [[nodiscard]] vk::Extent2D getExtent() const { return extent; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // Get per-swapchain-image composite framebuffer
        [[nodiscard]] vk::Framebuffer getCompositeFramebuffer(uint32_t imageIndex) const;

    private:
        core::Device& device;

        // Distortion buffer (R16G16_SFLOAT)
        vk::Image distortionImage;
        core::VulkanAllocation distortionAllocation;
        vk::ImageView distortionView;

        // Scene color copy (same format as swapchain)
        vk::Image sceneColorCopyImage;
        core::VulkanAllocation sceneColorCopyAllocation;
        vk::ImageView sceneColorCopyView;

        vk::Sampler linearSampler;

        // Render passes
        vk::RenderPass distortionVectorRenderPass;
        vk::RenderPass compositeRenderPass;

        // Framebuffers
        vk::Framebuffer distortionVectorFramebuffer;
        std::vector<vk::Framebuffer> compositeFramebuffers;

        // Descriptors for composite pass
        vk::DescriptorSetLayout compositeDescriptorSetLayout;
        vk::DescriptorPool compositeDescriptorPool;
        vk::DescriptorSet compositeDescriptorSet;

        vk::Extent2D extent{};
        bool initialized = false;

        void createDistortionImage(uint32_t width, uint32_t height);
        void createSceneColorCopyImage(vk::Format format, uint32_t width, uint32_t height);
        void createSampler();
        void createDistortionVectorRenderPass(vk::Format depthFormat);
        void createCompositeRenderPass(vk::Format colorFormat);
        void createDistortionVectorFramebuffer(vk::ImageView depthView);
        void createCompositeFramebuffers(const core::OffscreenResources& offscreen);
        void createCompositeDescriptorLayout();
        void createCompositeDescriptorPool();
        void allocateCompositeDescriptorSet();
        void updateCompositeDescriptorSet();
        void transitionImagesInitial();
    };
}
