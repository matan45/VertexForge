#pragma once

#include "../compute/GPUVFXTypes.hpp"
#include "VFXMeshMaterialResolver.hpp"
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
        // VK-1481: empty (0-binding) layout used to pad sets 1-3 when the lighting layouts are absent,
        // so the shared bindless texture set always lands at set 4 regardless of lighting init order.
        vk::DescriptorSetLayout emptySetLayout;
        vk::DescriptorPool descriptorPool;

        vk::Buffer cameraUBO;
        core::VulkanAllocation cameraUBOAllocation;
        void* cameraUBOMapped = nullptr;

        // VK-1526: per-emitter PBR material SSBO (set 0, binding 5), indexed by emitterIndex. Host-visible
        // mapped; slots are written on setEmitterMaterial/removeEmitter (not per-frame). materialFlags == 0
        // (the zero-initialized default) => the mesh shader takes the legacy single-.vfImage path.
        vk::Buffer materialSlotsBuffer;
        core::VulkanAllocation materialSlotsAllocation;
        void* materialSlotsMapped = nullptr;

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

            // VK-1526: optional PBR material for mesh particles. When set, the mesh shader shades PBR from
            // these maps + scalars (materialSlots) instead of the single texturePath albedo. The resolved
            // map paths are retained so removeEmitter/re-config can release the exact bindless refs they took.
            std::string matAlbedoPath;
            std::string matNormalPath;
            std::string matOrmPath;
            std::string matEmissivePath;
            VFXMeshMaterialSlots materialSlots{}; // GPU payload; materialFlags == 0 => legacy single-.vfImage path
        };

        std::unordered_map<uint32_t, EmitterMeshData> emitterMeshes;
        std::unordered_map<uint32_t, EmitterRenderConfig> emitterConfigs;
        vk::DescriptorSet defaultDescriptorSet; // VK-1481: the single set-0 (camera/particle/config/depth)

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
        // VK-1526: assign (or clear) an emitter's PBR material. `resolved.hasMaterial == false` clears it,
        // restoring the single-.vfImage path. Called at (re)configure time, not per-frame.
        void setEmitterMaterial(uint32_t emitterIndex, const ResolvedVFXMeshMaterial& resolved);
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

        // VK-1526: release the bindless refs held by an emitter's material maps and clear the stored paths.
        void releaseMaterialMaps(EmitterRenderConfig& config);
        // VK-1526: copy a 64-byte material slot into the mapped SSBO at emitterIndex (no-op if out of range).
        void uploadMaterialSlot(uint32_t emitterIndex, const VFXMeshMaterialSlots& slots) const;
    };
}
