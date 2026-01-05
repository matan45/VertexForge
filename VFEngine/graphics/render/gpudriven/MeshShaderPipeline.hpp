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

    private:
        void createPerDrawDataDescriptor();
        void createMeshletDataDescriptor();
        void createVertexDataDescriptor();
        void createMeshShaderGraphicsPipeline(vk::DescriptorSetLayout iblLayout,
                                               vk::DescriptorSetLayout bindlessTextureLayout,
                                               vk::RenderPass renderPass);
    };
}
