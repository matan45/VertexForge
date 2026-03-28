#pragma once

#include "LightCullingTypes.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::lighting
{
    class LightCullingPipeline
    {
    private:
        core::Device& device;

        // Shader
        std::unique_ptr<core::Shader> shader;

        // Pipeline resources
        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // Output buffers (DEVICE_LOCAL)
        vk::Buffer clusterLightGridBuffer;       // Per-cluster offset+count
        core::VulkanAllocation clusterLightGridAllocation;

        vk::Buffer clusterLightIndexListBuffer;  // Flat array of light indices
        core::VulkanAllocation clusterLightIndexListAllocation;

        vk::Buffer globalsBuffer;                // Atomic counters and stats
        core::VulkanAllocation globalsAllocation;

        // External descriptor set layouts (not owned)
        vk::DescriptorSetLayout clusterGridLayout;   // Set 0: ClusterGridManager
        vk::DescriptorSetLayout lightBufferLayout;   // Set 1: GPULightBufferManager

        // Cached external descriptor sets for binding
        vk::DescriptorSet cachedClusterGridDescSet;
        vk::DescriptorSet cachedLightBufferDescSet;

        bool initialized = false;
        bool descriptorsNeedUpdate = true;
        uint32_t totalClusters = 0;

    public:
        explicit LightCullingPipeline(core::Device& device);
        ~LightCullingPipeline();

        LightCullingPipeline(const LightCullingPipeline&) = delete;
        LightCullingPipeline& operator=(const LightCullingPipeline&) = delete;

        void init(uint32_t clusterCount,
                  vk::DescriptorSetLayout clusterGridDescLayout,
                  vk::DescriptorSetLayout lightBufferDescLayout);
        void cleanup();

        // Update external descriptor sets for binding
        void updateExternalDescriptors(
            vk::DescriptorSet clusterGridDescSet,
            vk::DescriptorSet lightBufferDescSet
        );

        // Execute light culling (call within command buffer recording)
        void dispatch(
            vk::CommandBuffer cmd,
            const glm::mat4& viewMatrix,
            uint32_t pointLightCount,
            uint32_t spotLightCount
        );

        [[nodiscard]] vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        [[nodiscard]] vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }
        [[nodiscard]] vk::Buffer getClusterLightGridBuffer() const { return clusterLightGridBuffer; }
        [[nodiscard]] vk::Buffer getClusterLightIndexListBuffer() const { return clusterLightIndexListBuffer; }
        [[nodiscard]] vk::Buffer getGlobalsBuffer() const { return globalsBuffer; }

        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] uint32_t getTotalClusters() const { return totalClusters; }

    private:
        void createBuffers();
        void destroyBuffers();
        void createDescriptorSetLayout();
        void createPipelineLayout();
        void createComputePipeline();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void writeDescriptors();

        void dispatchReset(vk::CommandBuffer cmd);
        void dispatchLightCulling(vk::CommandBuffer cmd, const glm::mat4& viewMatrix,
                                  uint32_t pointLightCount, uint32_t spotLightCount);
        void insertBarrier(vk::CommandBuffer cmd);
    };
}
