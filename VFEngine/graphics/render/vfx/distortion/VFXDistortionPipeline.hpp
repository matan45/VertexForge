#pragma once

#include "../compute/GPUVFXTypes.hpp"
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
    class VFXDistortionPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        bool initialized = false;
        mutable bool descriptorsNeedUpdate = true;

        std::shared_ptr<core::Shader> distortionShader;

        vk::Format colorFormat = vk::Format::eUndefined;
        vk::Format depthFormat = vk::Format::eUndefined;
        vk::Pipeline graphicsPipeline;
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
        void* cameraUBOMapped = nullptr;

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

        // Per-emitter distortion texture
        static constexpr uint32_t MAX_TEXTURE_SLOTS = 64;

        struct DistortionEmitterConfig
        {
            std::string texturePath;
            float distortionStrength = 0.1f;
        };

        struct TextureEntry
        {
            std::unique_ptr<core::Texture> texture;
            vk::DescriptorSet descriptorSet;
            uint32_t refCount = 0;
        };

        std::unordered_map<uint32_t, DistortionEmitterConfig> emitterConfigs;
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
        explicit VFXDistortionPipeline(core::Device& device, core::SwapChain& swapChain);
        ~VFXDistortionPipeline();

        VFXDistortionPipeline(const VFXDistortionPipeline&) = delete;
        VFXDistortionPipeline& operator=(const VFXDistortionPipeline&) = delete;

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

        void setEmitterDistortionTexture(uint32_t emitterIndex, const std::string& texturePath);
        void setEmitterDistortionConfig(uint32_t emitterIndex, float strength);
        void removeEmitter(uint32_t emitterIndex);

        void setDeletionQueue(core::DeferredDeletionQueue* dq) { deletionQueue = dq; }

        void recordCommandsInline(
            vk::CommandBuffer cmd,
            vk::Buffer drawCommandBuffer,
            uint32_t emitterCount,
            const std::vector<bool>& distortionEnabledFlags) const;

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
