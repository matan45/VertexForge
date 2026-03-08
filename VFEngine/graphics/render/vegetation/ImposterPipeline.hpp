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
    class ImposterPipeline
    {
    public:
        ImposterPipeline() = default;
        ~ImposterPipeline();

        ImposterPipeline(const ImposterPipeline&) = delete;
        ImposterPipeline& operator=(const ImposterPipeline&) = delete;

        void init(core::Device& device,
                  vk::DescriptorSetLayout cameraLayout,
                  vk::DescriptorSetLayout bindlessTextureLayout,
                  vk::RenderPass renderPass);

        void cleanup();

        void recreate(vk::DescriptorSetLayout cameraLayout,
                      vk::DescriptorSetLayout bindlessTextureLayout,
                      vk::RenderPass renderPass);

        // Set 0 descriptors: visible LOD2 buffer + count + tree instances
        void updateInstanceDescriptors(vk::Buffer visibleBuffer,
                                        vk::Buffer visibleCountBuffer,
                                        vk::Buffer treeInstanceBuffer);

        // Set 3 descriptor: imposter config buffer
        void updateImposterConfigDescriptor(vk::Buffer imposterConfigBuffer);

        void updateSharedDescriptors(vk::DescriptorSet cameraDescSet,
                                      vk::DescriptorSet bindlessTextureDescSet);

        void dispatch(vk::CommandBuffer cmd, uint32_t visibleCount);

        bool isInitialized() const { return initialized; }

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

        // Set 3: imposter config (owned)
        vk::DescriptorSetLayout imposterConfigLayout;
        vk::DescriptorPool imposterConfigPool;
        vk::DescriptorSet imposterConfigDescriptorSet;

        // External layouts (not owned)
        vk::DescriptorSetLayout cachedCameraLayout;
        vk::DescriptorSetLayout cachedBindlessTextureLayout;

        // External descriptor sets (not owned)
        vk::DescriptorSet cameraDescriptorSet;
        vk::DescriptorSet bindlessTextureDescriptorSet;

        bool initialized = false;

        void createOwnedDescriptors();
        void createPipeline(vk::DescriptorSetLayout cameraLayout,
                            vk::DescriptorSetLayout bindlessTextureLayout,
                            vk::RenderPass renderPass);
        bool loadShaders();
    };
}
