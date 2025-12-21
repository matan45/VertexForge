#pragma once
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include "math/Frustum.hpp"

namespace core {
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::occlusion {
    
    class HiZBuffer {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        
        vk::Image hiZImage;
        vk::DeviceMemory hiZMemory;
        vk::ImageView hiZImageView;  // Full mip chain view
        std::vector<vk::ImageView> mipViews;  // Per-mip views for compute shader
        vk::Sampler hiZSampler;
        
        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        std::vector<vk::DescriptorSet> descriptorSets;  // One per mip level transition
        
        vk::Image sourceDepthImage;
        vk::ImageView sourceDepthView;
        vk::Sampler depthSampler;

        std::unique_ptr<core::Shader> shader;

        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipLevels = 0;
        vk::Format depthFormat = vk::Format::eUndefined;

        bool initialized = false;
        
    public:
        explicit HiZBuffer(core::Device& device, core::SwapChain& swapChain);
        ~HiZBuffer();
        
        void init(vk::Image depthImage, vk::ImageView depthImageView, vk::Format depthFormat);

        void generate(vk::CommandBuffer cmd);
        
        vk::ImageView getHiZImageView() const { return hiZImageView; }
        vk::Sampler getHiZSampler() const { return hiZSampler; }
        uint32_t getMipLevels() const { return mipLevels; }
        
        void cleanup();

        bool isInitialized() const { return initialized; }

    private:
        void createHiZImage();
        void createHiZSampler();
        void createComputePipeline();
        void createDescriptorSets();
    };

}
