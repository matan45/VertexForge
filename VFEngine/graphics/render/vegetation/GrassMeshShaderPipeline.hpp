#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <cstdint>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::vegetation
{
    struct GrassMeshPushConstants
    {
        glm::vec4 baseColor;
        glm::vec4 tipColor;
        float fadeStartDistance;
        float fadeEndDistance;
    };

    class GrassMeshShaderPipeline
    {
    private:
        core::Device* devicePtr = nullptr;

        std::unique_ptr<core::Shader> grassShader;

        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;

        vk::DescriptorSetLayout grassDataLayout;    // Set 0: instance buffer + count
        vk::DescriptorSetLayout cameraLayout;       // Set 1: camera UBO
        vk::DescriptorSetLayout windLayout;          // Set 2: wind UBO

        vk::DescriptorPool grassDataPool;
        vk::DescriptorSet grassDataDescriptorSet;

        vk::DescriptorSet cameraDescriptorSet;
        vk::DescriptorSet windDescriptorSet;

        bool initialized = false;

    public:
        GrassMeshShaderPipeline();
        ~GrassMeshShaderPipeline();

        GrassMeshShaderPipeline(const GrassMeshShaderPipeline&) = delete;
        GrassMeshShaderPipeline& operator=(const GrassMeshShaderPipeline&) = delete;

        void init(core::Device& device,
                  vk::DescriptorSetLayout cameraLayout,
                  vk::DescriptorSetLayout windLayout,
                  vk::RenderPass renderPass);

        void cleanup();

        void recreate(vk::DescriptorSetLayout cameraLayout,
                      vk::DescriptorSetLayout windLayout,
                      vk::RenderPass renderPass);

        void updateGrassDataDescriptors(vk::Buffer grassInstanceBuffer,
                                         vk::Buffer grassCountBuffer);

        void updateSharedDescriptors(vk::DescriptorSet cameraDescSet,
                                      vk::DescriptorSet windDescSet);

        void dispatch(vk::CommandBuffer cmd,
                      uint32_t instanceCount,
                      float fadeStartDistance,
                      float fadeEndDistance,
                      const glm::vec4& baseColor,
                      const glm::vec4& tipColor);

        bool isInitialized() const { return initialized; }

    private:
        void createGrassDataDescriptor();
        void createGrassPipeline(vk::DescriptorSetLayout cameraLayout,
                                  vk::DescriptorSetLayout windLayout,
                                  vk::RenderPass renderPass);
        bool loadGrassShaders();
    };
}
