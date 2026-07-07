#pragma once

#include "../compute/GPUVFXTypes.hpp"
#include "vfx/VFXBlendMode.hpp"
#include "vfx/VFXSortOrder.hpp"
#include "../../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

    class VFXRibbonGPUPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        bool initialized = false;
        mutable bool descriptorsNeedUpdate = true;

        std::shared_ptr<core::Shader> gpuShader;

        vk::Format colorFormat = vk::Format::eUndefined;
        vk::Format depthFormat = vk::Format::eUndefined;
        vk::Pipeline graphicsPipeline;
        vk::Pipeline multiplyPipeline; // VK-1472: Multiply blend variant (shares pipelineLayout)
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        // VK-1481: empty (0-binding) layout used to pad sets 1-3 when the lighting layouts are absent,
        // so the shared bindless texture set always lands at set 4 regardless of lighting init order.
        vk::DescriptorSetLayout emptySetLayout;
        vk::DescriptorPool descriptorPool;

        vk::Buffer quadVertexBuffer;
        core::VulkanAllocation quadVertexBufferAllocation;
        vk::Buffer quadIndexBuffer;
        core::VulkanAllocation quadIndexBufferAllocation;
        vk::Buffer cameraUBO;
        core::VulkanAllocation cameraUBOAllocation;
        void* cameraUBOMapped = nullptr;

        // VK-1481 Phase 2 (draw-call merge): per-emitter render-data SSBO (host-visible + mapped),
        // holding the data a merged multi-draw can't push (textureIndex/alphaClip/blendMode/glow),
        // indexed by emitter slot. Written each frame in recordCommandsInline; read at set 0 binding 8.
        vk::Buffer renderDataBuffer;
        core::VulkanAllocation renderDataBufferAllocation;
        void* renderDataMapped = nullptr;

        vk::Buffer cachedParticleBuffer;
        vk::DeviceSize cachedParticleBufferSize = 0;

        vk::Buffer cachedConfigBuffer;
        vk::DeviceSize cachedConfigBufferSize = 0;

        vk::Buffer cachedRibbonRingBuffer;
        vk::DeviceSize cachedRibbonRingBufferSize = 0;

        vk::Buffer cachedRibbonHeadBuffer;
        vk::DeviceSize cachedRibbonHeadBufferSize = 0;

        // VK-1474: baked LUT (width curve ch7 / tail gradient ch8), shared with the compute sim
        // buffer. Bound at descriptor binding 7 (vertex stage) so the ribbon shader can sample it.
        vk::Buffer cachedLutBuffer;
        vk::DeviceSize cachedLutBufferSize = 0;

        vk::Image defaultTextureImage;
        core::VulkanAllocation defaultTextureAllocation;
        vk::ImageView defaultTextureImageView;
        vk::Sampler textureSampler;

        vk::ImageView sceneDepthImageView;
        vk::Sampler depthSampler;

        struct EmitterRenderConfig
        {
            std::string texturePath;
            float alphaClipThreshold = 0.1f;
            uint32_t blendMode = 0;
            glm::vec3 glowColor{1.0f, 1.0f, 1.0f};
            int32_t sortOrder = 0;     // VK-1471: per-emitter draw-order key
            uint32_t textureIndex = 0; // VK-1481: bindless slot for this emitter (0 = white default)
        };

        std::unordered_map<uint32_t, EmitterRenderConfig> emitterConfigs;
        vk::DescriptorSet defaultDescriptorSet; // VK-1481: the single set-0 (camera/particle/config/depth/ring/head/lut)

        // VK-1481 Phase 2: per-frame reused scratch for the merged draw (reserve-once, cleared each
        // frame) — avoids per-frame heap allocations on the render hot path. recordCommandsInline is
        // the sole (render-thread) writer, hence mutable on the const record path.
        mutable std::vector<::vfx::VFXDrawOrderEntry> scratchDrawOrder;
        mutable std::vector<uint32_t> scratchSlots;
        mutable std::vector<uint32_t> scratchKeys;

        // VK-1481: shared VFX bindless texture table (owned by the VFX renderer); bound at set 4.
        VFXBindlessTextures* bindless = nullptr;

        // Lighting descriptor sets (shared from main renderer)
        vk::DescriptorSetLayout lightBufferLayout;
        vk::DescriptorSetLayout clusterGridLayout;
        vk::DescriptorSetLayout clusterLightGridLayout;
        vk::DescriptorSet cachedLightBufferSet;
        vk::DescriptorSet cachedClusterGridSet;
        vk::DescriptorSet cachedClusterLightGridSet;
        bool lightingAvailable = false;

    public:
        explicit VFXRibbonGPUPipeline(core::Device& device, core::SwapChain& swapChain);
        ~VFXRibbonGPUPipeline();

        VFXRibbonGPUPipeline(const VFXRibbonGPUPipeline&) = delete;
        VFXRibbonGPUPipeline& operator=(const VFXRibbonGPUPipeline&) = delete;

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
        void updateRibbonBuffers(vk::Buffer ringBuffer, vk::DeviceSize ringBufferSize,
                                  vk::Buffer headBuffer, vk::DeviceSize headBufferSize);
        void updateLutBuffer(vk::Buffer lutBuffer, vk::DeviceSize lutBufferSize); // VK-1474

        void setEmitterTexture(uint32_t emitterIndex, const std::string& texturePath);
        void setEmitterRenderingConfig(uint32_t emitterIndex, float alphaClipThreshold, ::vfx::VFXBlendMode blendMode,
                                       const glm::vec3& glowColor = glm::vec3(1.0f), int32_t sortOrder = 0);
        void removeEmitter(uint32_t emitterIndex);

        // VK-1481: inject the shared bindless texture table. Must be set BEFORE init() (createPipeline
        // appends its descriptor set layout at set 4).
        void setBindlessTextures(VFXBindlessTextures* b) { bindless = b; }

        void setLightingLayouts(vk::DescriptorSetLayout lightBuffer,
                                vk::DescriptorSetLayout clusterGrid,
                                vk::DescriptorSetLayout clusterLightGrid);
        void updateLightingDescriptorSets(vk::DescriptorSet lightBuffer,
                                          vk::DescriptorSet clusterGrid,
                                          vk::DescriptorSet clusterLightGrid);

        void recordCommandsInline(
            vk::CommandBuffer cmd,
            vk::Buffer drawCommandBuffer,
            uint32_t emitterCount,
            uint32_t frameIndex) const;

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
