#pragma once

#include "../../../core/VulkanMemoryManager.hpp"
#include "../../raytracing/RTShadowMaskSet.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::gpudriven
{
    class MeshletBuffer;
    class MergedMeshBuffer;

    struct MeshPipelineInitInfo
    {
        vk::DescriptorSetLayout iblLayout;
        vk::DescriptorSetLayout bindlessTextureLayout;
        vk::DescriptorSetLayout boneMatrixLayout;
        vk::DescriptorSetLayout lightDataLayout;
        vk::DescriptorSetLayout clusterGridLayout;
        vk::DescriptorSetLayout cullingOutputLayout;
        vk::DescriptorSetLayout shadowDataLayout;
        vk::DescriptorSetLayout shadowTextureLayout;
        vk::DescriptorSetLayout giProbeDataLayout;
        vk::DescriptorSetLayout causticLayout;
        vk::DescriptorSetLayout rtShadowMaskLayout;
        // Plugin world-space mask (set 11, or set 14 when GI occupies 11)
        vk::DescriptorSetLayout worldMaskLayout;
        // Optional per-spot-light RT shadow mask array (set 15, VK-1175)
        vk::DescriptorSetLayout rtSpotShadowMaskLayout;
        // Optional per-point-light RT shadow mask array (set 16, VK-1176)
        vk::DescriptorSetLayout rtPointShadowMaskLayout;
        // Editor selection coverage (fixed set 15 when present).
        vk::DescriptorSetLayout selectionCoverageLayout;
        // Dynamic rendering formats (Vulkan 1.3)
        std::vector<vk::Format> colorAttachmentFormats;
        vk::Format depthAttachmentFormat = vk::Format::eUndefined;
        bool transparentMode = false;
        bool wboitMode = false;
    };

    struct MeshShaderPushConstants
    {
        uint32_t baseDrawIndex;
        uint32_t viewMode;
        float screenWidth;
        float screenHeight;
        uint32_t hiZMipLevels;
    };

    // viewMode bit packing: bits 0-7 = viewMode, bit 8 = frustum culling, bit 9 = backface culling, bit 11 = occlusion culling
    constexpr uint32_t MESHLET_CULL_FRUSTUM_BIT = 0x100;
    constexpr uint32_t MESHLET_CULL_BACKFACE_BIT = 0x200;
    constexpr uint32_t MESHLET_CULL_OCCLUSION_BIT = 0x800;
    constexpr uint32_t SELECTION_COVERAGE_WRITE_BIT = 0x1000;

    struct MeshletCullingStats
    {
        uint32_t totalMeshlets;
        uint32_t culledByFrustum;
        uint32_t culledByBackface;
        uint32_t visibleMeshlets;
        uint32_t culledByOcclusion;
    };

    class MeshShaderPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<core::Shader> meshShader;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;

        vk::DescriptorSetLayout perDrawDataLayout;
        vk::DescriptorPool perDrawDataPool;
        vk::DescriptorSet perDrawDataDescriptorSet;

        // VK-1209: material SVT sampling. When enabled, set 1 (perDrawData) gains 3 SSBO bindings
        // (3=page table, 4=feedback, 5=image info) and the SVT_ENABLED macro is compiled in — the
        // BC7 atlas is sampled via the bindless heap, so no new descriptor set is added anywhere.
        bool svtSampleEnabled = false;

        vk::DescriptorSetLayout meshletDataLayout;
        vk::DescriptorPool meshletDataPool;
        vk::DescriptorSet meshletDataDescriptorSet;

        vk::DescriptorSetLayout vertexDataLayout;
        vk::DescriptorPool vertexDataPool;
        vk::DescriptorSet vertexDataDescriptorSet;

        vk::Buffer statsBuffer;
        core::VulkanAllocation statsBufferAllocation;
        MeshletCullingStats cachedStats{};

        vk::DescriptorSet lightDataDescriptorSet;
        vk::DescriptorSet clusterGridDescriptorSet;
        vk::DescriptorSet cullingOutputDescriptorSet;

        vk::DescriptorSet shadowDataDescriptorSet;
        vk::DescriptorSet shadowTextureDescriptorSet;
        vk::DescriptorSet giProbeDataDescriptorSet;
        vk::DescriptorSet causticDescriptorSet;
        vk::DescriptorSet worldMaskDescriptorSet;
        // All three optional RT shadow masks (directional/spot/point) share one set at set 13
        // (bindings 0/1/2), freeing sets 15/16 so the layout needs at most 14 bound sets.
        std::unique_ptr<raytracing::RTShadowMaskSet> rtMaskSet;
        // Last producer descriptor handed to each updateRT*ShadowMaskDescriptor. Retained so a
        // pipeline rebuild can re-copy them into a freshly (re)created rtMaskSet without depending
        // on the renderer re-issuing the updates afterward (TerrainMeshShaderPipeline does the same).
        vk::DescriptorSet rtDirectionalMaskProducer;
        vk::DescriptorSet rtSpotMaskProducer;
        vk::DescriptorSet rtPointMaskProducer;
        // VK-1398: per-image-slot dirty flags. updateRT*ShadowMaskDescriptor only caches the producer
        // and marks all slots dirty; the actual copyInto into rtMaskSet happens lazily in
        // ensureRTMaskSlot() at bind time, writing only the slot for the image being recorded (whose
        // prior submission has retired) — never a slot bound by an in-flight command buffer.
        std::array<bool, core::MAX_SWAPCHAIN_IMAGES> rtMaskSlotDirty{};
        void markAllRTMaskSlotsDirty() { rtMaskSlotDirty.fill(true); }

        vk::DescriptorSetLayout cachedLightDataLayout;
        vk::DescriptorSetLayout cachedClusterGridLayout;
        vk::DescriptorSetLayout cachedCullingOutputLayout;

        vk::DescriptorSetLayout cachedShadowDataLayout;
        vk::DescriptorSetLayout cachedShadowTextureLayout;
        vk::DescriptorSetLayout cachedGIProbeDataLayout;
        vk::DescriptorSetLayout cachedCausticLayout;

        vk::DescriptorSetLayout emptyPlaceholderLayout;

        bool isTransparentMode = false;
        bool rtMaskBound = false;         // set 13 (shared directional/spot/point mask) present
        bool worldMaskLayoutBound = false;
        uint32_t worldMaskSetIndex = 0;   // 11, or 14 when GI occupies set 11
        bool isWBOITMode = false;
        bool isWireframeMode = false;
    public:
        explicit MeshShaderPipeline(core::Device& device, core::SwapChain& swapChain);
        ~MeshShaderPipeline();

        MeshShaderPipeline(const MeshShaderPipeline&) = delete;
        MeshShaderPipeline& operator=(const MeshShaderPipeline&) = delete;

        void setWireframeMode(bool enabled) { isWireframeMode = enabled; }

        void init(const MeshPipelineInitInfo& info);
        void cleanup();
        void recreate(const MeshPipelineInitInfo& info);

        void updatePerDrawDescriptor(vk::Buffer perDrawDataBuffer);

        // VK-1209: enable the SVT sample path (set-1 bindings 3/4/5 + SVT_ENABLED). Takes effect on
        // the next (re)create. updateSVTResources writes the page table / feedback / image-info SSBOs.
        void setSVTSampleEnabled(bool enabled) { svtSampleEnabled = enabled; }
        bool isSVTSampleEnabled() const { return svtSampleEnabled; }
        void updateSVTResources(vk::Buffer pageTableBuffer, vk::Buffer feedbackBuffer, vk::Buffer imageInfoBuffer);
        void updateInstanceTransformDescriptor(vk::Buffer instanceTransformBuffer);
        void updateObjectBufferDescriptor(vk::Buffer objectBuffer);
        void updateMeshletDescriptors(MeshletBuffer& meshletBuffer);
        void updateHiZDescriptor(vk::ImageView hiZView, vk::Sampler hiZSampler);
        void updateVertexDescriptors(MergedMeshBuffer& mergedBuffer);

        void updateLightingDescriptors(vk::DescriptorSet lightDataDescSet,
                                       vk::DescriptorSet clusterGridDescSet,
                                       vk::DescriptorSet cullingOutputDescSet);

        void updateShadowDescriptors(vk::DescriptorSet shadowDataDescSet,
                                     vk::DescriptorSet shadowTextureDescSet);

        void updateGIProbeDescriptor(vk::DescriptorSet giProbeDescSet);
        void updateCausticDescriptor(vk::DescriptorSet causticDescSet);
        void updateRTShadowMaskDescriptor(vk::DescriptorSet rtShadowMaskDescSet);
        void updateWorldMaskDescriptor(vk::DescriptorSet worldMaskDescSet);
        void updateRTSpotShadowMaskDescriptor(vk::DescriptorSet rtSpotShadowMaskDescSet);
        void updateRTPointShadowMaskDescriptor(vk::DescriptorSet rtPointShadowMaskDescSet);

        vk::Pipeline getPipeline() const { return graphicsPipeline; }
        vk::PipelineLayout getPipelineLayout() const { return pipelineLayout; }
        vk::DescriptorSet getPerDrawDataDescriptorSet() const { return perDrawDataDescriptorSet; }
        vk::DescriptorSet getMeshletDataDescriptorSet() const { return meshletDataDescriptorSet; }
        vk::DescriptorSet getVertexDataDescriptorSet() const { return vertexDataDescriptorSet; }
        vk::DescriptorSet getLightDataDescriptorSet() const { return lightDataDescriptorSet; }
        vk::DescriptorSet getClusterGridDescriptorSet() const { return clusterGridDescriptorSet; }
        vk::DescriptorSet getCullingOutputDescriptorSet() const { return cullingOutputDescriptorSet; }
        vk::DescriptorSet getShadowDataDescriptorSet() const { return shadowDataDescriptorSet; }
        vk::DescriptorSet getShadowTextureDescriptorSet() const { return shadowTextureDescriptorSet; }
        vk::DescriptorSet getGIProbeDataDescriptorSet() const { return giProbeDataDescriptorSet; }
        vk::DescriptorSet getCausticDescriptorSet() const { return causticDescriptorSet; }
        // Shared RT shadow mask set (set 13): directional binding 0, spot binding 1, point binding 2.
        // Ringed per swapchain image (VK-1398) — pass the image index being recorded. Call
        // ensureRTMaskSlot(imageIndex) first to lazily populate that slot from the cached producers.
        void ensureRTMaskSlot(uint32_t imageIndex);
        vk::DescriptorSet getRTMaskDescriptorSet(uint32_t imageIndex) const
        {
            return rtMaskSet ? rtMaskSet->getDescriptorSet(imageIndex) : nullptr;
        }
        bool hasRTMask() const { return rtMaskBound; }
        vk::DescriptorSet getWorldMaskDescriptorSet() const { return worldMaskDescriptorSet; }
        bool hasWorldMaskLayout() const { return worldMaskLayoutBound; }
        uint32_t getWorldMaskSetIndex() const { return worldMaskSetIndex; }

        vk::DescriptorSetLayout getPerDrawDataLayout() const { return perDrawDataLayout; }
        vk::DescriptorSetLayout getMeshletDataLayout() const { return meshletDataLayout; }
        vk::DescriptorSetLayout getVertexDataLayout() const { return vertexDataLayout; }

        void resetStats(vk::CommandBuffer cmd);
        MeshletCullingStats readStats();

    private:
        void createStatsBuffer();
        void createPerDrawDataDescriptor();
        void createMeshletDataDescriptor();
        void createVertexDataDescriptor();

        void createMeshShaderGraphicsPipeline(const MeshPipelineInitInfo& info);
    };
}
