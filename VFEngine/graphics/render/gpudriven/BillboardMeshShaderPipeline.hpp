#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>
#include <cstdint>

namespace core
{
    class Device;
    class Shader;
}

namespace render::gpudriven
{
    class BillboardMeshShaderPipeline
    {
    private:
        core::Device* devicePtr = nullptr;
        std::unique_ptr<core::Shader> taskShader;
        std::unique_ptr<core::Shader> meshFragShader;

        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;

        // Set 0: instance data (owned)
        vk::DescriptorSetLayout instanceDataLayout;
        vk::DescriptorPool instanceDataPool;
        vk::DescriptorSet instanceDataDescriptorSet;

        // Set 1: camera UBO (owned)
        vk::DescriptorSetLayout cameraLayout;
        vk::DescriptorPool cameraPool;
        vk::DescriptorSet cameraDescriptorSet;

        // External layouts (not owned)
        vk::DescriptorSetLayout cachedBindlessTextureLayout;

        // External descriptor sets (not owned)
        vk::DescriptorSet bindlessTextureDescriptorSet;

        bool initialized = false;
    public:
        explicit BillboardMeshShaderPipeline();
        ~BillboardMeshShaderPipeline();

        BillboardMeshShaderPipeline(const BillboardMeshShaderPipeline&) = delete;
        BillboardMeshShaderPipeline& operator=(const BillboardMeshShaderPipeline&) = delete;

        void init(core::Device& device,
                  vk::DescriptorSetLayout cameraLayout,
                  vk::DescriptorSetLayout bindlessTextureLayout,
                  vk::RenderPass renderPass);

        void cleanup();

        void recreate(vk::DescriptorSetLayout cameraLayout,
                      vk::DescriptorSetLayout bindlessTextureLayout,
                      vk::RenderPass renderPass);

        // Set 0 descriptors: billboard instance buffer + count buffer
        void updateInstanceDescriptors(vk::Buffer instanceBuffer, vk::Buffer countBuffer);

        void updateCameraDescriptor(vk::Buffer cameraBuffer);

        void updateSharedDescriptors(vk::DescriptorSet bindlessTextureDescSet);

        void dispatch(vk::CommandBuffer cmd, uint32_t instanceCount);

        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] vk::Pipeline getPipeline() const { return graphicsPipeline; }

    private:

        void createOwnedDescriptors();
        void createPipeline(vk::DescriptorSetLayout cameraLayout,
                            vk::DescriptorSetLayout bindlessTextureLayout,
                            vk::RenderPass renderPass);
        bool loadShaders();
    };
}
