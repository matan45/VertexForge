#pragma once

#include "../../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
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
        // Dynamic rendering formats (Vulkan 1.3)
        std::vector<vk::Format> colorAttachmentFormats;
        vk::Format depthAttachmentFormat = vk::Format::eUndefined;
        bool transparentMode = false;
        bool wboitMode = false;
        bool motionVectorsEnabled = false;
    };

    struct MeshShaderPushConstants
    {
        uint32_t baseDrawIndex;
        uint32_t viewMode;
        float screenWidth;
        float screenHeight;
        uint32_t hiZMipLevels;
        uint32_t _pad0;
        uint32_t _pad1;
        uint32_t _pad2;
        glm::mat4 prevViewProjection; // For per-vertex motion vectors (offset 32, 64 bytes)
    };

    // viewMode bit packing: bits 0-7 = viewMode, bit 8 = frustum culling, bit 9 = backface culling, bit 11 = occlusion culling
    constexpr uint32_t MESHLET_CULL_FRUSTUM_BIT = 0x100;
    constexpr uint32_t MESHLET_CULL_BACKFACE_BIT = 0x200;
    constexpr uint32_t MESHLET_CULL_OCCLUSION_BIT = 0x800;

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
        vk::DescriptorSet rtShadowMaskDescriptorSet;

        vk::DescriptorSetLayout cachedLightDataLayout;
        vk::DescriptorSetLayout cachedClusterGridLayout;
        vk::DescriptorSetLayout cachedCullingOutputLayout;

        vk::DescriptorSetLayout cachedShadowDataLayout;
        vk::DescriptorSetLayout cachedShadowTextureLayout;
        vk::DescriptorSetLayout cachedGIProbeDataLayout;
        vk::DescriptorSetLayout cachedCausticLayout;

        vk::DescriptorSetLayout emptyPlaceholderLayout;

        bool isTransparentMode = false;
        bool rtShadowLayoutBound = false;
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
        vk::DescriptorSet getRTShadowMaskDescriptorSet() const { return rtShadowMaskDescriptorSet; }
        bool hasRTShadowLayout() const { return rtShadowLayoutBound; }

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
