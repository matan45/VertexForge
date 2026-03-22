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
        // Distance-based density fadeout
        float cameraX;
        float cameraZ;
        float densityFadeStart;
        float densityFadeEnd;
        float minDensityScale;
        // Multi-type vegetation
        uint32_t vegetationType;         // 0=Grass, 1=Billboard
        uint32_t billboardTextureIndex;  // Bindless texture index for this dispatch
        uint32_t billboardMode;          // 0=Cross, 1=CameraFacing
    };

    class GrassComputePipeline
    {
    private:
        core::Device* devicePtr = nullptr;

        std::unique_ptr<core::Shader> shader;

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
    };
}
