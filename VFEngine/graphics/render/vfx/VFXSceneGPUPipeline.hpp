#pragma once

#include "VFXBillboardTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <string>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
    class Texture;
}

namespace render::vfx
{
    // GPU-driven VFX rendering pipeline
    // Uses indirect draw with particle data from SSBO
    // Works with GPUVFXComputePipeline and GPUVFXBufferManager
    class VFXSceneGPUPipeline
    {
    private:
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        bool initialized = false;
        mutable bool descriptorsNeedUpdate = true;  // mutable: caching flag for lazy descriptor writes

        std::shared_ptr<core::Shader> gpuShader;

        vk::RenderPass externalRenderPass;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // Buffers
        vk::Buffer quadVertexBuffer;
        vk::DeviceMemory quadVertexBufferMemory;
        vk::Buffer quadIndexBuffer;
        vk::DeviceMemory quadIndexBufferMemory;
        vk::Buffer cameraUBO;
        vk::DeviceMemory cameraUBOMemory;
        void* cameraUBOMapped = nullptr;  // Persistently mapped for efficient per-frame updates

        // Cached particle buffer info
        vk::Buffer cachedParticleBuffer;
        vk::DeviceSize cachedParticleBufferSize = 0;

        // Default texture (white 1x1)
        vk::Image defaultTextureImage;
        vk::DeviceMemory defaultTextureMemory;
        vk::ImageView defaultTextureImageView;
        vk::Sampler textureSampler;

        // Custom texture support
        std::unique_ptr<core::Texture> customTexture;
        std::string currentTexturePath;

    public:
        explicit VFXSceneGPUPipeline(core::Device& device, core::SwapChain& swapChain);
        ~VFXSceneGPUPipeline();

        VFXSceneGPUPipeline(const VFXSceneGPUPipeline&) = delete;
        VFXSceneGPUPipeline& operator=(const VFXSceneGPUPipeline&) = delete;

        // Initialization
        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanup();
        bool isInitialized() const { return initialized; }

        // Update camera UBO
        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time) const;

        // Update particle buffer binding (called when buffer changes)
        void updateParticleBuffer(vk::Buffer particleBuffer, vk::DeviceSize particleBufferSize);

        // Set custom texture for particles
        void setTexture(const std::string& texturePath);

        // Record indirect draw commands inline within existing render pass
        void recordCommandsInline(
            vk::CommandBuffer cmd,
            vk::Buffer drawCommandBuffer,
            uint32_t emitterCount) const;

    private:
        // Internal helpers
        void loadShader();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void createPipeline();
        void createBuffers();
        void createDefaultTexture();
        void createSampler();
        void writeDescriptors() const;
    };
}
