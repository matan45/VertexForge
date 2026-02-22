#pragma once

#include "VFXBillboardTypes.hpp"
#include "GPUVFXTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <string>
#include <unordered_map>

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

        // Cached emitter config buffer info (VK-493)
        vk::Buffer cachedConfigBuffer;
        vk::DeviceSize cachedConfigBufferSize = 0;

        // Default texture (white 1x1)
        vk::Image defaultTextureImage;
        vk::DeviceMemory defaultTextureMemory;
        vk::ImageView defaultTextureImageView;
        vk::Sampler textureSampler;

        // Per-emitter texture and rendering config
        static constexpr uint32_t MAX_TEXTURE_SLOTS = 16;

        struct EmitterRenderConfig
        {
            std::string texturePath;
            float alphaClipThreshold = 0.1f;
            uint32_t blendMode = 0;
        };

        struct TextureEntry
        {
            std::unique_ptr<core::Texture> texture;
            vk::DescriptorSet descriptorSet;
        };

        std::unordered_map<uint32_t, EmitterRenderConfig> emitterConfigs;
        std::unordered_map<std::string, TextureEntry> textureEntries;
        vk::DescriptorSet defaultDescriptorSet;

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

        // Update emitter config buffer binding (VK-493)
        void updateConfigBuffer(vk::Buffer configBuffer, vk::DeviceSize configBufferSize);

        // Per-emitter texture and rendering config
        void setEmitterTexture(uint32_t emitterIndex, const std::string& texturePath);
        void setEmitterRenderingConfig(uint32_t emitterIndex, float alphaClipThreshold, bool additiveBlend);
        void removeEmitter(uint32_t emitterIndex);

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
        void writeDescriptorSet(vk::DescriptorSet dstSet, core::Texture* texture) const;
        vk::DescriptorSet allocateDescriptorSetFromPool();
    };
}
