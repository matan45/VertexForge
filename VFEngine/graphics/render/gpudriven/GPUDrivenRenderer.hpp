#pragma once

#include "GPUDrivenTypes.hpp"
#include "MergedMeshBuffer.hpp"
#include "IndirectBatchManager.hpp"
#include "BindlessTextureManager.hpp"
#include "GPUCullLODPipeline.hpp"
#include "GPUDrivenShaderCache.hpp"
#include "GPUDrivenCameraBuffer.hpp"
#include "GPUDrivenPipeline.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <unordered_set>

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
        std::unique_ptr<GPUDrivenShaderCache> customShaderCache;
        std::unique_ptr<GPUDrivenCameraBuffer> cameraBuffer;
        std::unique_ptr<GPUDrivenPipeline> pipeline;

        bool initialized = false;
        bool enabled = false;
        bool frustumCullingEnabled = true;
        bool lodSelectionEnabled = true;
        bool occlusionCullingEnabled = false; // Disabled by default until Hi-Z is set

        vk::ImageView cachedHiZView;
        vk::Sampler cachedHiZSampler;
        uint32_t hiZMipLevels = 0;

        GPUDrivenStats stats{};

        vk::DescriptorSetLayout cachedIBLLayout;
        vk::RenderPass cachedRenderPass;

        mesh::MaterialTextureCache* materialTextureCache = nullptr;

        std::unordered_set<std::string> registeredMaterialPaths;

        std::unordered_map<std::string, std::shared_ptr<material::MaterialData>> loadedMaterials;

        std::unique_ptr<mesh::MeshStreamManager> meshStreamManager;
        bool meshStreamingEnabled = true;

    public:
        explicit GPUDrivenRenderer(core::Device& device, core::SwapChain& swapChain);
        ~GPUDrivenRenderer();

        // Non-copyable
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

        void updateHiZPyramid(vk::ImageView hiZView, vk::Sampler hiZSampler, uint32_t mipLevels);

        const GPUDrivenStats& getStats() const { return stats; }

        void updateStatsFromGPU();

        bool isInitialized() const { return initialized; }

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

    private:
        bool registerMaterialTextures(const std::string& materialPath);
    };
}
