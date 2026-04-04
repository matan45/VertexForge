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
        float sssDistortion;
        float sssPower;
        float sssScale;
        uint32_t billboardTextureIndex;
    };

    struct GrassDispatchParams
    {
        uint32_t instanceCount = 0;
        float fadeStartDistance = 150.0f;
        float fadeEndDistance = 300.0f;
        glm::vec4 baseColor{0.1f, 0.4f, 0.05f, 1.0f};
        glm::vec4 tipColor{0.2f, 0.6f, 0.1f, 1.0f};
        float sssDistortion = 0.2f;
        float sssPower = 4.0f;
        float sssScale = 0.5f;
        uint32_t billboardTextureIndex = 0xFFFFFFFF;
    };

    class GrassMeshShaderPipeline
    {
    private:
        core::Device* devicePtr = nullptr;

        std::unique_ptr<core::Shader> grassShader;

        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;

        vk::DescriptorSetLayout grassDataLayout;    // Set 0: instance buffer + count
        vk::DescriptorSetLayout cameraLayout;       // Set 1: camera UBO (owned)
        vk::DescriptorSetLayout windLayout;          // Set 2: wind UBO
        vk::DescriptorSetLayout lightDataLayout;     // Set 3: light buffers
        vk::DescriptorSetLayout bindlessLayout;      // Set 4: bindless textures

        vk::DescriptorPool grassDataPool;
        vk::DescriptorSet grassDataDescriptorSet;

        vk::DescriptorPool cameraPool;
        vk::DescriptorSet cameraDescriptorSet;

        vk::DescriptorSet windDescriptorSet;
        vk::DescriptorSet lightDataDescriptorSet;
        vk::DescriptorSet bindlessDescriptorSet;

        bool initialized = false;

    public:
        GrassMeshShaderPipeline();
        ~GrassMeshShaderPipeline();

        GrassMeshShaderPipeline(const GrassMeshShaderPipeline&) = delete;
        GrassMeshShaderPipeline& operator=(const GrassMeshShaderPipeline&) = delete;

        void init(core::Device& device,
                  vk::DescriptorSetLayout windLayout,
                  vk::DescriptorSetLayout lightLayout,
                  vk::DescriptorSetLayout bindlessLayout,
                  const std::vector<vk::Format>& colorFormats, vk::Format depthFormat);

        void cleanup();

        void recreate(vk::DescriptorSetLayout windLayout,
                      vk::DescriptorSetLayout lightLayout,
                      vk::DescriptorSetLayout bindlessLayout,
                      const std::vector<vk::Format>& colorFormats, vk::Format depthFormat);

        void updateGrassDataDescriptors(vk::Buffer grassInstanceBuffer,
                                         vk::Buffer grassCountBuffer);

        void updateCameraDescriptor(vk::Buffer cameraBuffer);

        void updateSharedDescriptors(vk::DescriptorSet windDescSet,
                                      vk::DescriptorSet lightDescSet,
                                      vk::DescriptorSet bindlessDescSet);

        void dispatch(vk::CommandBuffer cmd, const GrassDispatchParams& params);

        bool isInitialized() const { return initialized; }

    private:
        void createGrassDataDescriptor();
        void createCameraDescriptor();
        void createPipelineLayout(vk::DescriptorSetLayout windLayout,
                                   vk::DescriptorSetLayout lightLayout,
                                   vk::DescriptorSetLayout bindlessLayout);
        void createGrassPipeline(vk::DescriptorSetLayout windLayout,
                                  vk::DescriptorSetLayout lightLayout,
                                  vk::DescriptorSetLayout bindlessLayout,
                                  const std::vector<vk::Format>& colorFormats, vk::Format depthFormat);
        void bindDescriptorSets(vk::CommandBuffer cmd);
        bool loadGrassShaders();
    };
}
