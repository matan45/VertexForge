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
    struct VegetationCullPushConstants
    {
        uint32_t totalInstances;
        uint32_t padding[3];
    };

    class VegetationCullLODPipeline
    {
    public:
        VegetationCullLODPipeline();
        ~VegetationCullLODPipeline();

        VegetationCullLODPipeline(const VegetationCullLODPipeline&) = delete;
        VegetationCullLODPipeline& operator=(const VegetationCullLODPipeline&) = delete;

        void init(core::Device& device);
        void cleanup();

        void updateDescriptors(
            vk::Buffer treeInstanceBuffer,
            vk::Buffer instanceCountBuffer,
            vk::Buffer visibleLOD0Buffer,
            vk::Buffer visibleLOD1Buffer,
            vk::Buffer visibleLOD2Buffer,
            vk::Buffer countersBuffer
        );

        void updateCameraDescriptor(vk::Buffer cameraBuffer);

        void dispatch(vk::CommandBuffer cmd, uint32_t instanceCount);

        bool isInitialized() const { return initialized; }

        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

    private:
        core::Device* devicePtr = nullptr;
        std::unique_ptr<core::Shader> shader;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;   // Set 0: buffers
        vk::DescriptorSetLayout cameraDescriptorSetLayout; // Set 1: camera
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;        // Set 0
        vk::DescriptorSet cameraDescriptorSet;  // Set 1

        bool initialized = false;
        bool descriptorsNeedUpdate = true;

        void createDescriptorSetLayouts();
        void createPipelineLayout();
        void createComputePipeline();
        void createDescriptorPool();
        void allocateDescriptorSets();
    };
}
