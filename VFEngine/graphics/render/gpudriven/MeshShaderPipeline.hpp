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


    struct MeshShaderPushConstants
    {
        uint32_t baseDrawIndex; // Base index into perDrawData buffer for this dispatch
        uint32_t viewMode;
        float screenWidth;
        float screenHeight;
    };

    // Separate push constants for task shader culling control
    // Note: Push constants are shared, so these flags are packed into viewMode bits
    // Bit 0-7: viewMode (0=Color, 1=Meshlet, 2=LOD)
    // Bit 8: enableFrustumCulling
    // Bit 9: enableBackfaceCulling
    constexpr uint32_t MESHLET_CULL_FRUSTUM_BIT = 0x100;
    constexpr uint32_t MESHLET_CULL_BACKFACE_BIT = 0x200;

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

        // Set 1: Per-draw data (written by compute, read by task/mesh/fragment)
        vk::DescriptorSetLayout perDrawDataLayout;
        vk::DescriptorPool perDrawDataPool;
        vk::DescriptorSet perDrawDataDescriptorSet;

        // Set 3: Meshlet data (meshlet buffer, vertex indices, primitive indices)
        vk::DescriptorSetLayout meshletDataLayout;
        vk::DescriptorPool meshletDataPool;
        vk::DescriptorSet meshletDataDescriptorSet;

        // Set 4: Merged vertex data (positions, normals, texcoords)
        vk::DescriptorSetLayout vertexDataLayout;
        vk::DescriptorPool vertexDataPool;
        vk::DescriptorSet vertexDataDescriptorSet;

        // Debug stats buffer (binding 3 in set 3)
        vk::Buffer statsBuffer;
        vk::DeviceMemory statsBufferMemory;
        MeshletCullingStats cachedStats{};

        // Set 6, 7, 8: Lighting descriptor sets (external, not owned)
        vk::DescriptorSet lightDataDescriptorSet;      // Set 6: Light buffers from GPULightBufferManager
        vk::DescriptorSet clusterGridDescriptorSet;    // Set 7: Cluster params from ClusterGridManager
        vk::DescriptorSet cullingOutputDescriptorSet;  // Set 8: Culling output from LightCullingPipeline

        // Cached lighting layouts for pipeline recreation
        vk::DescriptorSetLayout cachedLightDataLayout;
        vk::DescriptorSetLayout cachedClusterGridLayout;
        vk::DescriptorSetLayout cachedCullingOutputLayout;

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
                  vk::RenderPass renderPass);

        void cleanup();

        void recreate(vk::DescriptorSetLayout iblLayout,
                      vk::DescriptorSetLayout bindlessTextureLayout,
                      vk::DescriptorSetLayout boneMatrixLayout,
                      vk::DescriptorSetLayout lightDataLayout,
                      vk::DescriptorSetLayout clusterGridLayout,
                      vk::DescriptorSetLayout cullingOutputLayout,
                      vk::RenderPass renderPass);


        void updatePerDrawDescriptor(vk::Buffer perDrawDataBuffer);
        void updateMeshletDescriptors(MeshletBuffer& meshletBuffer);
        void updateVertexDescriptors(MergedMeshBuffer& mergedBuffer);

        // Update lighting descriptor sets (called each frame from GPUDrivenRenderer)
        void updateLightingDescriptors(vk::DescriptorSet lightDataDescSet,
                                       vk::DescriptorSet clusterGridDescSet,
                                       vk::DescriptorSet cullingOutputDescSet);

        vk::Pipeline getPipeline() const { return graphicsPipeline; }
        vk::PipelineLayout getPipelineLayout() const { return pipelineLayout; }
        vk::DescriptorSet getPerDrawDataDescriptorSet() const { return perDrawDataDescriptorSet; }
        vk::DescriptorSet getMeshletDataDescriptorSet() const { return meshletDataDescriptorSet; }
        vk::DescriptorSet getVertexDataDescriptorSet() const { return vertexDataDescriptorSet; }
        vk::DescriptorSet getLightDataDescriptorSet() const { return lightDataDescriptorSet; }
        vk::DescriptorSet getClusterGridDescriptorSet() const { return clusterGridDescriptorSet; }
        vk::DescriptorSet getCullingOutputDescriptorSet() const { return cullingOutputDescriptorSet; }

        // Layout accessors (for shadow pass pipeline initialization)
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
                                              vk::RenderPass renderPass);
    };
}
