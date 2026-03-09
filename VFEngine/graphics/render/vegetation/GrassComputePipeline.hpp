#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <cstdint>

namespace core
{
    class Device;
    class Shader;
}

namespace render::vegetation
{
    struct GrassComputePushConstants
    {
        glm::vec2 tileWorldOrigin;
        float tileWorldSize;
        float vertexSpacing;
        uint32_t verticesPerSide;
        uint32_t maxInstances;
        float slopeLimit;
        float densityMultiplier;
        float heightMin;
        float heightMax;
        float widthMin;
        float widthMax;
        float time;
    };

    class GrassComputePipeline
    {
    private:
        core::Device* devicePtr = nullptr;

        // Shader
        std::unique_ptr<core::Shader> shader;

        // Pipeline resources
        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        bool initialized = false;
        bool descriptorsNeedUpdate = true;

    public:
        GrassComputePipeline();
        ~GrassComputePipeline();

        // Non-copyable
        GrassComputePipeline(const GrassComputePipeline&) = delete;
        GrassComputePipeline& operator=(const GrassComputePipeline&) = delete;

        void init(core::Device& device);
        void cleanup();

        void updateDescriptors(
            vk::Buffer densityMapBuffer,
            vk::Buffer heightMapBuffer,
            vk::Buffer holeMaskBuffer,
            vk::Buffer grassInstanceBuffer,
            vk::Buffer counterBuffer
        );

        void dispatch(vk::CommandBuffer cmd, uint32_t texelCount,
                      const GrassComputePushConstants& pushConstants);

        bool isInitialized() const { return initialized; }

    private:
        void createDescriptorSetLayout();
        void createPipelineLayout();
        void createComputePipeline();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void writeDescriptors();
    };
}
