#pragma once

#include "GPUDrivenTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>

namespace core {
    class Device;
    class Shader;
}

namespace render::gpudriven {

    /**
     * Compute pipeline for GPU-driven culling and LOD selection.
     *
     * This pipeline processes GPUObjectData and outputs:
     * - VkDrawIndexedIndirectCommand for visible objects
     * - PerDrawData for vertex/fragment shaders
     * - Atomic draw count for vkCmdDrawIndexedIndirectCount
     */
    class GPUCullLODPipeline {
    public:
        explicit GPUCullLODPipeline(core::Device& device);
        ~GPUCullLODPipeline();

        // Non-copyable
        GPUCullLODPipeline(const GPUCullLODPipeline&) = delete;
        GPUCullLODPipeline& operator=(const GPUCullLODPipeline&) = delete;

        /**
         * Initialize the compute pipeline.
         * Compiles shaders and creates descriptor layouts.
         */
        void init();

        /**
         * Cleanup all GPU resources.
         */
        void cleanup();

        /**
         * Update descriptor bindings.
         * Must be called when buffers change or at least once before dispatch.
         *
         * @param objectBuffer GPUObjectData storage buffer
         * @param cameraBuffer GPUCameraData uniform buffer
         * @param drawCommandBuffer Output: VkDrawIndexedIndirectCommand array
         * @param perDrawDataBuffer Output: PerDrawData array
         * @param drawCountBuffer Output: Atomic draw count (single uint32_t)
         */
        void updateDescriptors(
            vk::Buffer objectBuffer,
            vk::Buffer cameraBuffer,
            vk::Buffer drawCommandBuffer,
            vk::Buffer perDrawDataBuffer,
            vk::Buffer drawCountBuffer
        );

        /**
         * Update Hi-Z texture binding for occlusion culling.
         * @param hiZView Hi-Z pyramid image view (full mip chain)
         * @param hiZSampler Sampler for Hi-Z texture (nearest filtering)
         */
        void updateHiZDescriptor(vk::ImageView hiZView, vk::Sampler hiZSampler);

        /**
         * Dispatch the compute shader.
         *
         * @param cmd Command buffer to record into
         * @param objectCount Number of objects to process
         */
        void dispatch(vk::CommandBuffer cmd, uint32_t objectCount);

        /**
         * Get the descriptor set layout for external binding.
         */
        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

        /**
         * Check if the pipeline is initialized.
         */
        bool isInitialized() const { return initialized; }

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

        bool initialized = false;
        bool descriptorsNeedUpdate = true;

        // Cached buffer info for descriptor updates
        vk::Buffer cachedObjectBuffer;
        vk::Buffer cachedCameraBuffer;
        vk::Buffer cachedDrawCommandBuffer;
        vk::Buffer cachedPerDrawDataBuffer;
        vk::Buffer cachedDrawCountBuffer;

        // Cached Hi-Z descriptor info
        vk::ImageView cachedHiZView;
        vk::Sampler cachedHiZSampler;
        bool hiZDescriptorNeedsUpdate = false;

        // Helper methods
        void createDescriptorSetLayout();
        void createPipelineLayout();
        void createComputePipeline();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void writeDescriptors();
    };

}
