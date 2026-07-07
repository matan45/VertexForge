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
    class VFXBindlessTextures;

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

        // VK-1481 Phase 2 (draw-call merge): per-emitter render-data SSBO (host-visible + mapped),
        // holding the per-emitter distortionStrength/textureIndex a merged multi-draw can't push,
        // indexed by emitter slot. Written each frame in recordCommandsInline; read at set 0 binding 8.
        vk::Buffer renderDataBuffer;
        core::VulkanAllocation renderDataBufferAllocation;
        void* renderDataMapped = nullptr;

        // Cached particle buffer info
        vk::Buffer cachedParticleBuffer;
        vk::DeviceSize cachedParticleBufferSize = 0;
        vk::Buffer cachedConfigBuffer;
        vk::DeviceSize cachedConfigBufferSize = 0;

        // Default texture (neutral-normal 1x1) — still the binding-4 scene-depth fallback.
        vk::Image defaultTextureImage;
        core::VulkanAllocation defaultTextureAllocation;
        vk::ImageView defaultTextureImageView;
        vk::Sampler textureSampler;

        vk::ImageView sceneDepthImageView;
        vk::Sampler depthSampler;

        // Per-emitter distortion texture and rendering config
        struct DistortionEmitterConfig
        {
            std::string distortionTexturePath;
            float distortionStrength = 0.1f;
            uint32_t textureIndex = 0; // VK-1481: bindless slot (0 = unset; neutral-normal used at record time)
        };

        std::unordered_map<uint32_t, DistortionEmitterConfig> emitterConfigs;
        vk::DescriptorSet defaultDescriptorSet; // VK-1481: the single set-0 (camera/particle/config/depth)

        // VK-1481: shared VFX bindless texture table (owned by VFXSceneRenderer); bound at set 1
        // (distortion has no lighting sets, so the local set 0 is the only one before it).
        VFXBindlessTextures* bindless = nullptr;

        // Deferred cleanup queue (kept for wiring symmetry; texture lifetime now lives in the bindless table)
        core::DeferredDeletionQueue* deletionQueue = nullptr;

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

        // VK-1481: inject the shared bindless texture table. Must be set BEFORE init() (createPipeline
        // appends its descriptor set layout at set 1).
        void setBindlessTextures(VFXBindlessTextures* b) { bindless = b; }

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
        void writeDescriptorSet(vk::DescriptorSet dstSet) const;
    };
}
