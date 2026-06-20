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

        vk::DescriptorSetLayout instanceDataLayout;
        vk::DescriptorPool instanceDataPool;
        vk::DescriptorSet instanceDataDescriptorSet;

        vk::DescriptorSetLayout cameraLayout;
        vk::DescriptorPool cameraPool;
        vk::DescriptorSet cameraDescriptorSet;

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
                  const std::vector<vk::Format>& colorFormats, vk::Format depthFormat);

        void cleanup();

        void recreate(vk::DescriptorSetLayout cameraLayout,
                      vk::DescriptorSetLayout bindlessTextureLayout,
                      const std::vector<vk::Format>& colorFormats, vk::Format depthFormat);

        void updateInstanceDescriptors(vk::Buffer instanceBuffer, vk::Buffer countBuffer);

        void updateCameraDescriptor(vk::Buffer cameraBuffer);

        void updateSharedDescriptors(vk::DescriptorSet bindlessTextureDescSet);

        void dispatch(vk::CommandBuffer cmd, uint32_t instanceCount, float time);

        [[nodiscard]] bool isInitialized() const { return initialized; }


    private:

        void createOwnedDescriptors();
        void createPipeline(vk::DescriptorSetLayout cameraLayout,
                            vk::DescriptorSetLayout bindlessTextureLayout,
                            const std::vector<vk::Format>& colorFormats, vk::Format depthFormat);
        bool loadShaders();
    };
}
