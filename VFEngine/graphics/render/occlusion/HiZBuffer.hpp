#pragma once
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>
#include "math/Frustum.hpp"

namespace core {
    class Device;
    class SwapChain;
}

namespace render::occlusion {

    // Hierarchical Z-Buffer for occlusion culling
    class HiZBuffer {
    public:
        HiZBuffer(core::Device& device, core::SwapChain& swapChain);
        ~HiZBuffer();

        // Initialize Hi-Z resources (call after depth buffer is created)
        void init(vk::Image depthImage, vk::ImageView depthImageView, vk::Format depthFormat);

        // Generate Hi-Z pyramid from current depth buffer
        void generate(vk::CommandBuffer cmd);

        // Test if an AABB is occluded (returns true if visible)
        // Uses CPU readback of Hi-Z - for debugging/validation
        bool testAABBVisible(const math::AABB& worldAABB,
                            const glm::mat4& viewProj,
                            float nearPlane) const;

        // Get Hi-Z texture for GPU-based occlusion testing
        vk::ImageView getHiZImageView() const { return hiZImageView; }
        vk::Sampler getHiZSampler() const { return hiZSampler; }
        uint32_t getMipLevels() const { return mipLevels; }

        // Cleanup
        void cleanup();

        bool isInitialized() const { return initialized; }

    private:
        void createHiZImage();
        void createHiZSampler();
        void createComputePipeline();
        void createDescriptorSets();

        core::Device& device;
        core::SwapChain& swapChain;

        // Hi-Z texture with mipmap chain
        vk::Image hiZImage;
        vk::DeviceMemory hiZMemory;
        vk::ImageView hiZImageView;  // Full mip chain view
        std::vector<vk::ImageView> mipViews;  // Per-mip views for compute shader
        vk::Sampler hiZSampler;

        // Compute pipeline for Hi-Z generation
        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        std::vector<vk::DescriptorSet> descriptorSets;  // One per mip level transition

        // Source depth
        vk::Image sourceDepthImage;
        vk::ImageView sourceDepthView;
        vk::Sampler depthSampler;

        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipLevels = 0;
        vk::Format depthFormat = vk::Format::eUndefined;

        bool initialized = false;
    };

}
