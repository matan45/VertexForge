#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>
#include <cstdint>

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

    // Push constant for mesh shader pipeline
    struct MeshShaderPushConstants {
        uint32_t baseDrawIndex;  // Base index into perDrawData buffer for this dispatch
        uint32_t viewMode;       // 0=Color, 1=Meshlet, 2=LOD
        float screenWidth;       // Screen width in pixels (for debug visualization)
        float screenHeight;      // Screen height in pixels (for debug visualization)
    };

    // Separate push constants for task shader culling control
    // Note: Push constants are shared, so these flags are packed into viewMode bits
    // Bit 0-7: viewMode (0=Color, 1=Meshlet, 2=LOD)
    // Bit 8: enableFrustumCulling
    // Bit 9: enableBackfaceCulling
    constexpr uint32_t MESHLET_CULL_FRUSTUM_BIT = 0x100;
    constexpr uint32_t MESHLET_CULL_BACKFACE_BIT = 0x200;

    // Debug statistics for meshlet culling (must match shader struct)
    struct MeshletCullingStats {
        uint32_t totalMeshlets;       // Total meshlets processed
        uint32_t culledByFrustum;     // Meshlets culled by frustum test
        uint32_t culledByBackface;    // Meshlets culled by backface cone test
        uint32_t visibleMeshlets;     // Meshlets that passed all tests
    };

    // Pipeline for GPU-driven mesh shader rendering
    // Uses Task + Mesh + Fragment shader pipeline instead of traditional vertex/fragment
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

    public:
        explicit MeshShaderPipeline(core::Device& device, core::SwapChain& swapChain);
        ~MeshShaderPipeline();

        MeshShaderPipeline(const MeshShaderPipeline&) = delete;
        MeshShaderPipeline& operator=(const MeshShaderPipeline&) = delete;

        // Initialize the pipeline
        // iblLayout: Set 0 - Camera UBO + IBL textures
        // bindlessTextureLayout: Set 2 - Bindless texture array
        void init(vk::DescriptorSetLayout iblLayout,
                  vk::DescriptorSetLayout bindlessTextureLayout,
                  vk::RenderPass renderPass);

        void cleanup();

        void recreate(vk::DescriptorSetLayout iblLayout,
                      vk::DescriptorSetLayout bindlessTextureLayout,
                      vk::RenderPass renderPass);

        // Update descriptor sets with buffer references
        void updatePerDrawDescriptor(vk::Buffer perDrawDataBuffer);
        void updateMeshletDescriptors(MeshletBuffer& meshletBuffer);
        void updateVertexDescriptors(MergedMeshBuffer& mergedBuffer);

        // Getters
        vk::Pipeline getPipeline() const { return graphicsPipeline; }
        vk::PipelineLayout getPipelineLayout() const { return pipelineLayout; }
        vk::DescriptorSetLayout getPerDrawDataLayout() const { return perDrawDataLayout; }
        vk::DescriptorSet getPerDrawDataDescriptorSet() const { return perDrawDataDescriptorSet; }
        vk::DescriptorSetLayout getMeshletDataLayout() const { return meshletDataLayout; }
        vk::DescriptorSet getMeshletDataDescriptorSet() const { return meshletDataDescriptorSet; }
        vk::DescriptorSetLayout getVertexDataLayout() const { return vertexDataLayout; }
        vk::DescriptorSet getVertexDataDescriptorSet() const { return vertexDataDescriptorSet; }

        // Debug stats
        void resetStats(vk::CommandBuffer cmd);
        MeshletCullingStats readStats();
        const MeshletCullingStats& getCachedStats() const { return cachedStats; }

    private:
        void createStatsBuffer();
        void createPerDrawDataDescriptor();
        void createMeshletDataDescriptor();
        void createVertexDataDescriptor();
        void createMeshShaderGraphicsPipeline(vk::DescriptorSetLayout iblLayout,
                                               vk::DescriptorSetLayout bindlessTextureLayout,
                                               vk::RenderPass renderPass);
    };
}
