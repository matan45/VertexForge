#pragma once

#include "ClusterBufferTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::gpudriven
{
    class ClusterBuffer;

    // =========================================================================
    // ClusterDAGTraversalPipeline - Compute pipeline for DAG cluster traversal
    //
    // Handles the multi-pass DAG traversal for Nanite-style cluster selection:
    // 1. Root enqueue: Initialize work queue with root clusters
    // 2. Traverse passes: Process work queue, select clusters or enqueue children
    // =========================================================================

    class ClusterDAGTraversalPipeline
    {
    private:
        core::Device& device;

        // Shaders
        std::unique_ptr<core::Shader> rootEnqueueShader;
        std::unique_ptr<core::Shader> traverseShader;
        std::unique_ptr<core::Shader> prepareIndirectShader;

        // Pipelines
        vk::Pipeline rootEnqueuePipeline;
        vk::Pipeline traversePipeline;
        vk::Pipeline prepareIndirectPipeline;
        vk::PipelineLayout pipelineLayout;

        // VK-300: Separate layout for prepare indirect shader
        vk::DescriptorSetLayout prepareIndirectDescriptorSetLayout;
        vk::PipelineLayout prepareIndirectPipelineLayout;
        vk::DescriptorSet prepareIndirectDescriptorSet;

        // Descriptor resources
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // Traversal params buffer (per-frame parameters)
        vk::Buffer traversalParamsBuffer;
        vk::DeviceMemory traversalParamsBufferMemory;
        GPUClusterTraversalParams cachedParams{};

        // Cached buffer references for descriptor updates
        vk::Buffer cachedObjectBuffer;
        vk::Buffer cachedCameraBuffer;
        vk::Buffer cachedObjectDrawIndexBuffer;
        vk::ImageView cachedHiZView;
        vk::Sampler cachedHiZSampler;
        ClusterBuffer* cachedClusterBuffer = nullptr;

        bool initialized = false;
        bool descriptorsNeedUpdate = true;

        // Traversal configuration
        uint32_t maxPasses = 16;  // Maximum traversal passes per frame
        float screenErrorThreshold = 1.0f;  // Pixel error threshold
        float errorMultiplier = 1.0f;  // Global error scaling

    public:
        explicit ClusterDAGTraversalPipeline(core::Device& device);
        ~ClusterDAGTraversalPipeline();

        // Non-copyable
        ClusterDAGTraversalPipeline(const ClusterDAGTraversalPipeline&) = delete;
        ClusterDAGTraversalPipeline& operator=(const ClusterDAGTraversalPipeline&) = delete;

        // =========================================================================
        // Lifecycle
        // =========================================================================

        void init();
        void cleanup();

        // =========================================================================
        // Descriptor Updates
        // =========================================================================

        // Update all external buffer references
        void updateDescriptors(
            vk::Buffer objectBuffer,
            vk::Buffer cameraBuffer,
            vk::Buffer objectDrawIndexBuffer,
            ClusterBuffer& clusterBuffer
        );

        // Update Hi-Z texture reference
        void updateHiZDescriptor(vk::ImageView hiZView, vk::Sampler hiZSampler);

        // =========================================================================
        // Traversal Parameters
        // =========================================================================

        void setTraversalParams(
            float projectionFactor,
            float screenErrorThreshold,
            float errorMultiplier,
            uint32_t frameIndex,
            bool enableCulling,
            bool enableOcclusion
        );

        void setScreenErrorThreshold(float threshold) { screenErrorThreshold = threshold; }
        void setErrorMultiplier(float multiplier) { errorMultiplier = multiplier; }
        void setMaxPasses(uint32_t passes) { maxPasses = passes; }

        float getScreenErrorThreshold() const { return screenErrorThreshold; }
        float getErrorMultiplier() const { return errorMultiplier; }
        uint32_t getMaxPasses() const { return maxPasses; }

        // =========================================================================
        // Dispatch
        // =========================================================================

        // Dispatch the full DAG traversal
        // Returns the number of clusters selected
        void dispatch(
            vk::CommandBuffer cmd,
            uint32_t objectCount,
            ClusterBuffer& clusterBuffer
        );

        // =========================================================================
        // Query
        // =========================================================================

        bool isInitialized() const { return initialized; }
        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

    private:
        void createDescriptorSetLayout();
        void createPipelineLayout();
        void createRootEnqueuePipeline();
        void createTraversePipeline();
        void createPrepareIndirectPipeline();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void createTraversalParamsBuffer();
        void writeDescriptors();
        void writePrepareIndirectDescriptors();
        void uploadTraversalParams(vk::CommandBuffer cmd);
    };

} // namespace render::gpudriven
