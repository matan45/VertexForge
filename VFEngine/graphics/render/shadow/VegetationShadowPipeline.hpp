#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>
#include <glm/glm.hpp>

namespace core
{
    class Device;
    class Shader;
}

namespace render::shadow
{
    struct VegetationShadowPushConstants
    {
        glm::mat4 lightViewProjection;
        uint32_t instanceCount;
        uint32_t shadowLOD;
        float depthBias;
        float slopeBias;
    };

    class VegetationShadowPipeline
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Shader> shadowShader;
        vk::Pipeline pipeline;
        vk::PipelineLayout pipelineLayout;

        // Set 0: instance data (owned) — tree instances + count + species render info
        vk::DescriptorSetLayout instanceDataLayout;
        vk::DescriptorPool instanceDataPool;
        vk::DescriptorSet instanceDataDescriptorSet;

        vk::DescriptorSetLayout cachedMeshletDataLayout;
        vk::DescriptorSetLayout cachedVertexDataLayout;

        bool initialized = false;

    public:
        explicit VegetationShadowPipeline(core::Device& device);
        ~VegetationShadowPipeline();

        VegetationShadowPipeline(const VegetationShadowPipeline&) = delete;
        VegetationShadowPipeline& operator=(const VegetationShadowPipeline&) = delete;

        void init(vk::DescriptorSetLayout meshletDataLayout,
                  vk::DescriptorSetLayout vertexDataLayout,
                  vk::RenderPass shadowRenderPass);

        void cleanup();

        void updateInstanceDescriptors(vk::Buffer treeInstanceBuffer,
                                        vk::Buffer instanceCountBuffer,
                                        vk::Buffer speciesRenderInfoBuffer);

        void dispatch(vk::CommandBuffer cmd,
                      vk::DescriptorSet meshletDescSet,
                      vk::DescriptorSet vertexDescSet,
                      const glm::mat4& lightViewProjection,
                      uint32_t instanceCount,
                      uint32_t shadowLOD,
                      float depthBias,
                      float slopeBias);

        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createInstanceDataDescriptor();
        void createPipeline(vk::RenderPass shadowRenderPass);
    };
}
