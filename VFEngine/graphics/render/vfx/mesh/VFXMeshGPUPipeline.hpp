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

namespace render::mesh
{
    class MeshGPUCache;
}

namespace render::vfx
{
    class VFXBindlessTextures;

    class VFXMeshGPUPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        render::mesh::MeshGPUCache& meshCache;

        bool initialized = false;
        mutable bool descriptorsNeedUpdate = true;

        std::shared_ptr<core::Shader> gpuShader;

        vk::Format colorFormat = vk::Format::eUndefined;
        vk::Format depthFormat = vk::Format::eUndefined;
        vk::Pipeline graphicsPipeline;
        vk::Pipeline multiplyPipeline; // VK-1472: Multiply blend variant (shares pipelineLayout)
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;

        vk::Buffer cameraUBO;
        core::VulkanAllocation cameraUBOAllocation;
        void* cameraUBOMapped = nullptr;

        vk::Buffer cachedParticleBuffer;
        vk::DeviceSize cachedParticleBufferSize = 0;

        vk::Buffer cachedConfigBuffer;
        vk::DeviceSize cachedConfigBufferSize = 0;

        vk::Image defaultTextureImage;
        core::VulkanAllocation defaultTextureAllocation;
        vk::ImageView defaultTextureImageView;
        vk::Sampler textureSampler;

        vk::ImageView sceneDepthImageView;
        vk::Sampler depthSampler;

        struct EmitterMeshData
        {
            std::string meshPath;
            std::string meshId;
            vk::Buffer vertexBuffer;
            vk::Buffer indexBuffer;
            uint32_t indexCount = 0;
        };

        struct EmitterRenderConfig
        {
            std::string texturePath;
            float alphaClipThreshold = 0.1f;
            uint32_t blendMode = 0;
            glm::vec3 glowColor{1.0f, 1.0f, 1.0f};
            int32_t sortOrder = 0;     // VK-1471: per-emitter draw-order key
            uint32_t textureIndex = 0; // VK-1481: bindless slot for this emitter (0 = white default)
        };

        std::unordered_map<uint32_t, EmitterMeshData> emitterMeshes;
        std::unordered_map<uint32_t, EmitterRenderConfig> emitterConfigs;
        vk::DescriptorSet defaultDescriptorSet; // VK-1481: the single set-0 (camera/particle/config/depth)

        // VK-1481: shared VFX bindless texture table (owned by the VFX renderer); bound at set 4.
        VFXBindlessTextures* bindless = nullptr;

        // Deferred cleanup queue (kept for wiring symmetry; texture lifetime now lives in the bindless table)
        core::DeferredDeletionQueue* deletionQueue = nullptr;

        // Lighting descriptor sets (shared from main renderer)
        vk::DescriptorSetLayout lightBufferLayout;
        vk::DescriptorSetLayout clusterGridLayout;
        vk::DescriptorSetLayout clusterLightGridLayout;
        vk::DescriptorSet cachedLightBufferSet;
        vk::DescriptorSet cachedClusterGridSet;
        vk::DescriptorSet cachedClusterLightGridSet;
        bool lightingAvailable = false;

    public:
        explicit VFXMeshGPUPipeline(core::Device& device, core::SwapChain& swapChain,
                                     render::mesh::MeshGPUCache& meshCache);
        ~VFXMeshGPUPipeline();

        VFXMeshGPUPipeline(const VFXMeshGPUPipeline&) = delete;
        VFXMeshGPUPipeline& operator=(const VFXMeshGPUPipeline&) = delete;

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

        void setEmitterMesh(uint32_t emitterIndex, const std::string& meshPath);
        void setEmitterTexture(uint32_t emitterIndex, const std::string& texturePath);
        void setEmitterRenderingConfig(uint32_t emitterIndex, float alphaClipThreshold, ::vfx::VFXBlendMode blendMode,
                                       const glm::vec3& glowColor = glm::vec3(1.0f), int32_t sortOrder = 0);
        void removeEmitter(uint32_t emitterIndex);

        void setDeletionQueue(core::DeferredDeletionQueue* dq) { deletionQueue = dq; }

        // VK-1481: inject the shared bindless texture table. Must be set BEFORE init() (createPipeline
        // appends its descriptor set layout at set 4).
        void setBindlessTextures(VFXBindlessTextures* b) { bindless = b; }

        void setLightingLayouts(vk::DescriptorSetLayout lightBuffer,
                                vk::DescriptorSetLayout clusterGrid,
                                vk::DescriptorSetLayout clusterLightGrid);
        void updateLightingDescriptorSets(vk::DescriptorSet lightBuffer,
                                          vk::DescriptorSet clusterGrid,
                                          vk::DescriptorSet clusterLightGrid);

        uint32_t getEmitterMeshIndexCount(uint32_t emitterIndex) const;

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
        void writeDescriptorSet(vk::DescriptorSet dstSet) const;
    };
}
