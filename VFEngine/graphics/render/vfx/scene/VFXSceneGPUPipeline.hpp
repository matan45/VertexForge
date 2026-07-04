#pragma once

#include "../compute/GPUVFXTypes.hpp"
#include "vfx/VFXBlendMode.hpp"
#include "../../../core/VulkanMemoryManager.hpp"
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
    class VFXSceneGPUPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        bool initialized = false;
        mutable bool descriptorsNeedUpdate = true;  // mutable: caching flag for lazy descriptor writes

        std::shared_ptr<core::Shader> gpuShader;

        vk::Format colorFormat = vk::Format::eUndefined;
        vk::Format depthFormat = vk::Format::eUndefined;
        vk::Pipeline graphicsPipeline;
        vk::Pipeline multiplyPipeline; // VK-1472: Multiply blend variant (shares pipelineLayout)
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;

        // Buffers
        vk::Buffer quadVertexBuffer;
        core::VulkanAllocation quadVertexBufferAllocation;
        vk::Buffer quadIndexBuffer;
        core::VulkanAllocation quadIndexBufferAllocation;
        vk::Buffer cameraUBO;
        core::VulkanAllocation cameraUBOAllocation;
        void* cameraUBOMapped = nullptr;  // Persistently mapped for efficient per-frame updates

        // Cached particle buffer info
        vk::Buffer cachedParticleBuffer;
        vk::DeviceSize cachedParticleBufferSize = 0;

        vk::Buffer cachedConfigBuffer;
        vk::DeviceSize cachedConfigBufferSize = 0;

        // Default texture (white 1x1)
        vk::Image defaultTextureImage;
        core::VulkanAllocation defaultTextureAllocation;
        vk::ImageView defaultTextureImageView;
        vk::Sampler textureSampler;

        vk::ImageView sceneDepthImageView;
        vk::Sampler depthSampler;

        // Per-emitter texture and rendering config
        static constexpr uint32_t MAX_TEXTURE_SLOTS = 64;

        struct EmitterRenderConfig
        {
            std::string texturePath;
            float alphaClipThreshold = 0.1f;
            uint32_t blendMode = 0;
            uint32_t renderMode = 0;
            glm::vec3 glowColor{1.0f, 1.0f, 1.0f};
            bool distortionEnabled = false;
            int32_t sortOrder = 0; // VK-1471: per-emitter draw-order key
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

        // Deferred texture cleanup (avoids waitIdle GPU stall)
        core::DeferredDeletionQueue* deletionQueue = nullptr;

        struct PendingDescriptorSet
        {
            vk::DescriptorSet set;
            uint32_t frameRetired;
        };
        mutable uint32_t frameCounter = 0;
        mutable std::vector<PendingDescriptorSet> pendingDescriptorSets;

        // Lighting descriptor sets (shared from main renderer)
        vk::DescriptorSetLayout lightBufferLayout;
        vk::DescriptorSetLayout clusterGridLayout;
        vk::DescriptorSetLayout clusterLightGridLayout;
        vk::DescriptorSet cachedLightBufferSet;
        vk::DescriptorSet cachedClusterGridSet;
        vk::DescriptorSet cachedClusterLightGridSet;
        bool lightingAvailable = false;

    public:
        explicit VFXSceneGPUPipeline(core::Device& device, core::SwapChain& swapChain);
        ~VFXSceneGPUPipeline();

        VFXSceneGPUPipeline(const VFXSceneGPUPipeline&) = delete;
        VFXSceneGPUPipeline& operator=(const VFXSceneGPUPipeline&) = delete;

        void init(vk::Format colorFormat, vk::Format depthFormat);
        void recreate(vk::Format colorFormat, vk::Format depthFormat);
        void cleanup();
        bool isInitialized() const { return initialized; }

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time,
                             float nearPlane = 0.1f, float farPlane = 1000.0f) const;

        void setSceneDepthImageView(vk::ImageView depthView);

        void updateParticleBuffer(vk::Buffer particleBuffer, vk::DeviceSize particleBufferSize);
        void updateConfigBuffer(vk::Buffer configBuffer, vk::DeviceSize configBufferSize);

        void setEmitterTexture(uint32_t emitterIndex, const std::string& texturePath);
        void setEmitterRenderingConfig(uint32_t emitterIndex, float alphaClipThreshold, ::vfx::VFXBlendMode blendMode,
                                       const glm::vec3& glowColor = glm::vec3(1.0f), int32_t sortOrder = 0);
        void setEmitterRenderMode(uint32_t emitterIndex, uint32_t renderMode);
        void setEmitterDistortionEnabled(uint32_t emitterIndex, bool enabled);
        void removeEmitter(uint32_t emitterIndex);

        void setDeletionQueue(core::DeferredDeletionQueue* dq) { deletionQueue = dq; }

        void setLightingLayouts(vk::DescriptorSetLayout lightBuffer,
                                vk::DescriptorSetLayout clusterGrid,
                                vk::DescriptorSetLayout clusterLightGrid);
        void updateLightingDescriptorSets(vk::DescriptorSet lightBuffer,
                                          vk::DescriptorSet clusterGrid,
                                          vk::DescriptorSet clusterLightGrid);

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
