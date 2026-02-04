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

namespace render::gpudriven
{
    struct BrushPushConstants
    {
        glm::vec2 brushCenter;
        glm::vec2 tileWorldOrigin;
        float brushRadius;
        float brushStrength;
        float vertexSpacing;
        uint32_t vertexCount;
        uint32_t falloffType;
        uint32_t shapeType;
    };
    static_assert(sizeof(BrushPushConstants) == 40, "BrushPushConstants must be 40 bytes");

    class BrushComputePipeline
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Shader> shader;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        bool initialized = false;
        bool descriptorsNeedUpdate = true;

        vk::Buffer cachedHeightmapBuffer;
        vk::Buffer cachedInfluenceBuffer;

        static constexpr uint32_t WORKGROUP_SIZE = 8;

    public:
        explicit BrushComputePipeline(core::Device& device);
        ~BrushComputePipeline();

        BrushComputePipeline(const BrushComputePipeline&) = delete;
        BrushComputePipeline& operator=(const BrushComputePipeline&) = delete;

        void init();
        void cleanup();
        bool isInitialized() const { return initialized; }

        void updateDescriptors(vk::Buffer heightmapBuffer, vk::Buffer influenceBuffer);

        void dispatch(vk::CommandBuffer cmd, const BrushPushConstants& constants);

        void insertBarriersBeforeCompute(
            vk::CommandBuffer cmd,
            vk::Buffer heightmapBuffer,
            vk::Buffer influenceBuffer);

        void insertBarriersAfterCompute(
            vk::CommandBuffer cmd,
            vk::Buffer influenceBuffer);

    private:
        void createDescriptorSetLayout();
        void createPipelineLayout();
        void createComputePipeline();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void writeDescriptors();
    };
}
