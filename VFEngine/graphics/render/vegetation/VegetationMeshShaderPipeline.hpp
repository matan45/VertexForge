#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>
#include <cstdint>

namespace core
{
    class Device;
    class Shader;
}

namespace render::vegetation
{
    class VegetationMeshShaderPipeline
    {
    public:
        VegetationMeshShaderPipeline() = default;
        ~VegetationMeshShaderPipeline();

        VegetationMeshShaderPipeline(const VegetationMeshShaderPipeline&) = delete;
        VegetationMeshShaderPipeline& operator=(const VegetationMeshShaderPipeline&) = delete;

        void init(core::Device& device,
                  vk::DescriptorSetLayout cameraLayout,
                  vk::DescriptorSetLayout windLayout,
                  vk::DescriptorSetLayout meshletDataLayout,
                  vk::DescriptorSetLayout vertexDataLayout,
                  vk::RenderPass renderPass);

        void cleanup();

        void recreate(vk::DescriptorSetLayout cameraLayout,
                      vk::DescriptorSetLayout windLayout,
                      vk::DescriptorSetLayout meshletDataLayout,
                      vk::DescriptorSetLayout vertexDataLayout,
                      vk::RenderPass renderPass);

        // Set 0 descriptors: visible instance buffer + count + tree instance buffer
        void updateInstanceDescriptors(vk::Buffer visibleBuffer,
                                        vk::Buffer visibleCountBuffer,
                                        vk::Buffer treeInstanceBuffer);

        // External descriptor sets (owned elsewhere)
        void updateSharedDescriptors(vk::DescriptorSet cameraDescSet,
                                      vk::DescriptorSet windDescSet,
                                      vk::DescriptorSet meshletDescSet,
                                      vk::DescriptorSet vertexDescSet);

        void dispatch(vk::CommandBuffer cmd, uint32_t visibleCount);

        bool isInitialized() const { return initialized; }

    private:
        core::Device* devicePtr = nullptr;
        std::unique_ptr<core::Shader> vegShader;

        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;

        // Set 0: instance data (owned)
        vk::DescriptorSetLayout instanceDataLayout;
        vk::DescriptorPool instanceDataPool;
        vk::DescriptorSet instanceDataDescriptorSet;

        // External layouts (not owned)
        vk::DescriptorSetLayout cachedCameraLayout;
        vk::DescriptorSetLayout cachedWindLayout;
        vk::DescriptorSetLayout cachedMeshletDataLayout;
        vk::DescriptorSetLayout cachedVertexDataLayout;

        // External descriptor sets (not owned)
        vk::DescriptorSet cameraDescriptorSet;
        vk::DescriptorSet windDescriptorSet;
        vk::DescriptorSet meshletDescriptorSet;
        vk::DescriptorSet vertexDescriptorSet;

        bool initialized = false;

        void createInstanceDataDescriptor();
        void createPipeline(vk::DescriptorSetLayout cameraLayout,
                            vk::DescriptorSetLayout windLayout,
                            vk::DescriptorSetLayout meshletDataLayout,
                            vk::DescriptorSetLayout vertexDataLayout,
                            vk::RenderPass renderPass);
        bool loadShaders();
    };
}
