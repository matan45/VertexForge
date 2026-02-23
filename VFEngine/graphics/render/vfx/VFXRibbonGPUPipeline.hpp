#pragma once

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
    class DeferredDeletionQueue;
}

namespace render::vfx
{
    class VFXRibbonGPUPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        bool initialized = false;
        mutable bool descriptorsNeedUpdate = true;

        std::shared_ptr<core::Shader> gpuShader;

        vk::RenderPass externalRenderPass;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;

        vk::Buffer quadVertexBuffer;
        vk::DeviceMemory quadVertexBufferMemory;
        vk::Buffer quadIndexBuffer;
        vk::DeviceMemory quadIndexBufferMemory;
        vk::Buffer cameraUBO;
        vk::DeviceMemory cameraUBOMemory;
        void* cameraUBOMapped = nullptr;

        vk::Buffer cachedParticleBuffer;
        vk::DeviceSize cachedParticleBufferSize = 0;

        vk::Buffer cachedConfigBuffer;
        vk::DeviceSize cachedConfigBufferSize = 0;

        vk::Buffer cachedRibbonRingBuffer;
        vk::DeviceSize cachedRibbonRingBufferSize = 0;

        vk::Buffer cachedRibbonHeadBuffer;
        vk::DeviceSize cachedRibbonHeadBufferSize = 0;

        vk::Image defaultTextureImage;
        vk::DeviceMemory defaultTextureMemory;
        vk::ImageView defaultTextureImageView;
        vk::Sampler textureSampler;

        vk::ImageView sceneDepthImageView;
        vk::Sampler depthSampler;

        static constexpr uint32_t MAX_TEXTURE_SLOTS = 16;

        struct EmitterRenderConfig
        {
            std::string texturePath;
            float alphaClipThreshold = 0.1f;
            uint32_t blendMode = 0;
            glm::vec3 glowColor{1.0f, 1.0f, 1.0f};
        };

        struct TextureEntry
        {
            std::unique_ptr<core::Texture> texture;
            vk::DescriptorSet descriptorSet;
            uint32_t refCount = 0;
        };

        std::unordered_map<uint32_t, EmitterRenderConfig> emitterConfigs;
        std::unordered_map<std::string, TextureEntry> textureEntries;
        vk::DescriptorSet defaultDescriptorSet;

        core::DeferredDeletionQueue* deletionQueue = nullptr;

        struct PendingDescriptorSet
        {
            vk::DescriptorSet set;
            uint32_t frameRetired;
        };
        mutable uint32_t frameCounter = 0;
        mutable std::vector<PendingDescriptorSet> pendingDescriptorSets;

    public:
        explicit VFXRibbonGPUPipeline(core::Device& device, core::SwapChain& swapChain);
        ~VFXRibbonGPUPipeline();

        VFXRibbonGPUPipeline(const VFXRibbonGPUPipeline&) = delete;
        VFXRibbonGPUPipeline& operator=(const VFXRibbonGPUPipeline&) = delete;

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanup();
        bool isInitialized() const { return initialized; }

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time,
                             float nearPlane = 0.1f, float farPlane = 1000.0f) const;

        void setSceneDepthImageView(vk::ImageView depthView);

        void updateParticleBuffer(vk::Buffer particleBuffer, vk::DeviceSize particleBufferSize);
        void updateConfigBuffer(vk::Buffer configBuffer, vk::DeviceSize configBufferSize);
        void updateRibbonBuffers(vk::Buffer ringBuffer, vk::DeviceSize ringBufferSize,
                                  vk::Buffer headBuffer, vk::DeviceSize headBufferSize);

        void setEmitterTexture(uint32_t emitterIndex, const std::string& texturePath);
        void setEmitterRenderingConfig(uint32_t emitterIndex, float alphaClipThreshold, bool additiveBlend,
                                       const glm::vec3& glowColor = glm::vec3(1.0f));
        void removeEmitter(uint32_t emitterIndex);

        void setDeletionQueue(core::DeferredDeletionQueue* dq) { deletionQueue = dq; }

        void recordCommandsInline(
            vk::CommandBuffer cmd,
            vk::Buffer drawCommandBuffer,
            uint32_t emitterCount) const;

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void createPipeline();
        void createBuffers();
        void createDefaultTexture();
        void createSampler();
        void createDepthSampler();
        void writeDescriptors() const;
        void writeDescriptorSet(vk::DescriptorSet dstSet, core::Texture* texture) const;
        vk::DescriptorSet allocateDescriptorSetFromPool();
    };
}
