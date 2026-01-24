#pragma once

#include "GPUDrivenTypes.hpp"
#include "MergedMeshBuffer.hpp"
#include "IndirectBatchManager.hpp"
#include "BindlessTextureManager.hpp"
#include "GPUCullLODPipeline.hpp"
#include "GPUDrivenCameraBuffer.hpp"
#include "MeshShaderPipeline.hpp"
#include "MeshletBuffer.hpp"
#include "BoneMatrixManager.hpp"
#include "../lighting/GPULightBufferManager.hpp"
#include "../lighting/ClusterGridManager.hpp"
#include "../lighting/LightCullingPipeline.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <unordered_set>
#include <cstdint>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace material
{
    struct MaterialData;
}

namespace render::mesh
{
    class MaterialTextureCache;
    class MeshStreamManager;
    struct MeshRenderData;
}

namespace render::gpudriven
{
    class GPUDrivenRenderer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<MergedMeshBuffer> mergedBuffer;
        std::unique_ptr<IndirectBatchManager> batchManager;
        std::unique_ptr<BindlessTextureManager> bindlessTextures;
        std::unique_ptr<GPUCullLODPipeline> cullPipeline;
        std::unique_ptr<GPUDrivenCameraBuffer> cameraBuffer;
        std::unique_ptr<MeshShaderPipeline> meshShaderPipeline;
        std::unique_ptr<MeshletBuffer> meshletBuffer;
        std::unique_ptr<BoneMatrixManager> boneMatrixManager;
        std::unique_ptr<lighting::GPULightBufferManager> lightBufferManager;
        std::unique_ptr<lighting::ClusterGridManager> clusterGridManager;
        std::unique_ptr<lighting::LightCullingPipeline> lightCullingPipeline;

        bool initialized = false;
        bool enabled = false;
        bool frustumCullingEnabled = true;
        bool lodSelectionEnabled = true;
        bool occlusionCullingEnabled = true;

        bool meshletFrustumCullingEnabled = true;
        bool meshletBackfaceCullingEnabled = true;
        bool meshShaderSupported = false;
        uint32_t currentViewMode = 0;

        uint32_t hiZMipLevels = 0;

        GPUDrivenStats stats{};

        vk::DescriptorSetLayout cachedIBLLayout;
        vk::RenderPass cachedRenderPass;

        mesh::MaterialTextureCache* materialTextureCache = nullptr;

        std::unordered_set<std::string> registeredMaterialPaths;

        std::unordered_map<std::string, std::shared_ptr<material::MaterialData>> loadedMaterials;

        std::unique_ptr<mesh::MeshStreamManager> meshStreamManager;
        bool meshStreamingEnabled = true;

        // BVH-culled visible light entity IDs (set per-frame before dispatchCompute)
        std::unordered_set<uint32_t> visibleLightIds;
        bool useBVHLightCulling = false;

    public:
        explicit GPUDrivenRenderer(core::Device& device, core::SwapChain& swapChain);
        ~GPUDrivenRenderer();

        GPUDrivenRenderer(const GPUDrivenRenderer&) = delete;
        GPUDrivenRenderer& operator=(const GPUDrivenRenderer&) = delete;

        void init(vk::DescriptorSetLayout iblDescriptorSetLayout, vk::RenderPass renderPass);

        void cleanup();

        void setDefaultTexture(vk::ImageView view, vk::Sampler sampler);

        void updateScene(
            const std::vector<mesh::MeshRenderData>& opaqueObjects,
            const glm::mat4& view,
            const glm::mat4& projection,
            const glm::vec3& cameraPosition,
            float nearPlane,
            float farPlane,
            float time = 0.0f
        );

        void dispatchCompute(vk::CommandBuffer cmd);

        void renderDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet);

        void setEnabled(bool enabled) { this->enabled = enabled; }
        bool isEnabled() const { return enabled; }

        bool isFrustumCullingEnabled() const { return frustumCullingEnabled; }

        bool isLODSelectionEnabled() const { return lodSelectionEnabled; }

        void setOcclusionCullingEnabled(bool enabled) { occlusionCullingEnabled = enabled; }
        bool isOcclusionCullingEnabled() const { return occlusionCullingEnabled; }

        void setMeshletFrustumCullingEnabled(bool enabled) { meshletFrustumCullingEnabled = enabled; }
        bool isMeshletFrustumCullingEnabled() const { return meshletFrustumCullingEnabled; }
        void setMeshletBackfaceCullingEnabled(bool enabled) { meshletBackfaceCullingEnabled = enabled; }
        bool isMeshletBackfaceCullingEnabled() const { return meshletBackfaceCullingEnabled; }

        void setViewMode(uint32_t mode) { currentViewMode = mode; }
        uint32_t getViewMode() const { return currentViewMode; }

        void updateHiZPyramid(vk::ImageView hiZView, vk::Sampler hiZSampler, uint32_t mipLevels);

        const GPUDrivenStats& getStats() const { return stats; }

        void updateStatsFromGPU();

        MeshletCullingStats getMeshletCullingStats();

        void setMaterialTextureCache(mesh::MaterialTextureCache* cache) { materialTextureCache = cache; }

        uint32_t getHiZMipLevels() const { return hiZMipLevels; }

        void updateRenderPass(vk::RenderPass newRenderPass, vk::DescriptorSetLayout newIBLLayout = nullptr);

        uint32_t getMergedVertexCount() const;
        uint32_t getMergedIndexCount() const;
        uint32_t getRegisteredMeshCount() const;
        uint32_t getRegisteredTextureCount() const;


        uint32_t getBatchCount() const;
        uint32_t getCommandsPerBatch() const;
        uint32_t getTotalCapacity() const;
        uint64_t getDrawCommandBufferSize() const;
        uint64_t getDrawCountBufferSize() const;
        uint64_t getPerDrawDataBufferSize() const;
        uint64_t getTotalMemoryUsage() const;

        // Light buffer accessors (for future shader integration in VK-140)
        lighting::GPULightBufferManager* getLightBufferManager() const { return lightBufferManager.get(); }

        // Cluster grid accessors (for VK-139 light culling)
        lighting::ClusterGridManager* getClusterGridManager() const { return clusterGridManager.get(); }

        // Light culling accessors (for VK-140 shader integration)
        lighting::LightCullingPipeline* getLightCullingPipeline() const { return lightCullingPipeline.get(); }

        // Set visible lights from BVH frustum query (pre-culling before GPU upload)
        // Pass the result of LightBVH::queryFrustum() to only upload visible lights
        void setVisibleLightsFromBVH(const std::vector<uint32_t>& visibleLights);

        // Clear BVH light culling (revert to uploading all lights)
        void clearVisibleLights();

        // Check if BVH light culling is enabled
        bool isBVHLightCullingEnabled() const { return useBVHLightCulling; }

    private:
        bool registerMaterialTextures(const std::string& materialPath);
    };
}
