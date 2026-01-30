#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>

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
    class ClusterBuffer;

    struct MeshShaderPushConstants
    {
        uint32_t baseDrawIndex;
        uint32_t viewMode;
        float screenWidth;
        float screenHeight;
        // Cluster DAG mode fields (VK-293)
        uint32_t clusterMode;       // 0 = discrete LOD, 1 = cluster DAG
        uint32_t clusterBaseIndex;  // Base index into selection buffer
        uint32_t clusterCount;      // Number of clusters to process
        uint32_t padding;           // Alignment

        // SECURITY: Before dispatch in cluster DAG mode (clusterMode == 1), validate:
        //   clusterBaseIndex + clusterCount <= MAX_CLUSTER_SELECTIONS_PER_FRAME
        // The shader also validates against buffer bounds, but CPU validation
        // provides defense-in-depth and clearer error reporting.
    };

    // viewMode bit packing: bits 0-7 = viewMode, bit 8 = frustum culling, bit 9 = backface culling
    constexpr uint32_t MESHLET_CULL_FRUSTUM_BIT = 0x100;
    constexpr uint32_t MESHLET_CULL_BACKFACE_BIT = 0x200;

    // SECURITY: Validate cluster DAG push constants before dispatch
    // Returns true if valid, false if parameters would cause buffer overrun
    inline bool validateClusterDAGPushConstants(const MeshShaderPushConstants& pc,
                                                 uint32_t maxSelectionBufferSize)
    {
        // Check for overflow in addition
        if (pc.clusterBaseIndex > maxSelectionBufferSize) return false;
        if (pc.clusterCount > maxSelectionBufferSize - pc.clusterBaseIndex) return false;
        return true;
    }

    struct MeshletCullingStats
    {
        uint32_t totalMeshlets;
        uint32_t culledByFrustum;
        uint32_t culledByBackface;
        uint32_t visibleMeshlets;
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
        vk::DeviceMemory statsBufferMemory;
        MeshletCullingStats cachedStats{};

        vk::DescriptorSet lightDataDescriptorSet;
        vk::DescriptorSet clusterGridDescriptorSet;
        vk::DescriptorSet cullingOutputDescriptorSet;

        vk::DescriptorSet shadowDataDescriptorSet;
        vk::DescriptorSet shadowTextureDescriptorSet;

        vk::DescriptorSetLayout cachedLightDataLayout;
        vk::DescriptorSetLayout cachedClusterGridLayout;
        vk::DescriptorSetLayout cachedCullingOutputLayout;

        vk::DescriptorSetLayout cachedShadowDataLayout;
        vk::DescriptorSetLayout cachedShadowTextureLayout;

    public:
        explicit MeshShaderPipeline(core::Device& device, core::SwapChain& swapChain);
        ~MeshShaderPipeline();

        MeshShaderPipeline(const MeshShaderPipeline&) = delete;
        MeshShaderPipeline& operator=(const MeshShaderPipeline&) = delete;

        void init(vk::DescriptorSetLayout iblLayout,
                  vk::DescriptorSetLayout bindlessTextureLayout,
                  vk::DescriptorSetLayout boneMatrixLayout,
                  vk::DescriptorSetLayout lightDataLayout,
                  vk::DescriptorSetLayout clusterGridLayout,
                  vk::DescriptorSetLayout cullingOutputLayout,
                  vk::DescriptorSetLayout shadowDataLayout,
                  vk::DescriptorSetLayout shadowTextureLayout,
                  vk::RenderPass renderPass);

        void cleanup();

        void recreate(vk::DescriptorSetLayout iblLayout,
                      vk::DescriptorSetLayout bindlessTextureLayout,
                      vk::DescriptorSetLayout boneMatrixLayout,
                      vk::DescriptorSetLayout lightDataLayout,
                      vk::DescriptorSetLayout clusterGridLayout,
                      vk::DescriptorSetLayout cullingOutputLayout,
                      vk::DescriptorSetLayout shadowDataLayout,
                      vk::DescriptorSetLayout shadowTextureLayout,
                      vk::RenderPass renderPass);

        void updatePerDrawDescriptor(vk::Buffer perDrawDataBuffer);
        void updateMeshletDescriptors(MeshletBuffer& meshletBuffer);
        void updateVertexDescriptors(MergedMeshBuffer& mergedBuffer);
        void updateClusterDescriptors(ClusterBuffer& clusterBuffer);

        void updateLightingDescriptors(vk::DescriptorSet lightDataDescSet,
                                       vk::DescriptorSet clusterGridDescSet,
                                       vk::DescriptorSet cullingOutputDescSet);

        void updateShadowDescriptors(vk::DescriptorSet shadowDataDescSet,
                                     vk::DescriptorSet shadowTextureDescSet);

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
        void createMeshShaderGraphicsPipeline(vk::DescriptorSetLayout iblLayout,
                                              vk::DescriptorSetLayout bindlessTextureLayout,
                                              vk::DescriptorSetLayout boneMatrixLayout,
                                              vk::DescriptorSetLayout lightDataLayout,
                                              vk::DescriptorSetLayout clusterGridLayout,
                                              vk::DescriptorSetLayout cullingOutputLayout,
                                              vk::DescriptorSetLayout shadowDataLayout,
                                              vk::DescriptorSetLayout shadowTextureLayout,
                                              vk::RenderPass renderPass);
    };
}
