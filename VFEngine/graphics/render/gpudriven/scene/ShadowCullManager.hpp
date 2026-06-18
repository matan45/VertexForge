#pragma once

#include "../GPUDrivenTypes.hpp"
#include "../../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace core
{
    class Device;
}

namespace render::gpudriven
{
    class GPUCullLODPipeline;

    // Tier 4: per-shadow-view GPU culling (UE5-style compacted shadow buffer).
    //
    // Owns ONE shared shadow draw buffer (drawCommands + perDrawData) partitioned into
    // SHADOW_CULL_MAX_VIEWS fixed regions of SHADOW_CULL_DRAWS_PER_VIEW draws. Each rendered
    // shadow view (directional clipmap level / spot / point cube face) culls the scene against
    // its own frustum (gpu_cull_shadow.glsl, via GPUCullLODPipeline's shadow variant) and flat-
    // appends survivors into its region. The shadow pass then issues ONE indirect-count draw per
    // page over the page's view region (collapsing the old pages x sections loop), with the
    // shadow task shader doing the fine per-page crop-frustum + dual-layer filtering.
    //
    // Views with global index >= SHADOW_CULL_MAX_VIEWS fall back to the legacy main-camera buffer
    // (the recorder handles that), so this is always safe to enable.
    class ShadowCullManager
    {
    public:
        explicit ShadowCullManager(core::Device& device);
        ~ShadowCullManager();

        ShadowCullManager(const ShadowCullManager&) = delete;
        ShadowCullManager& operator=(const ShadowCullManager&) = delete;

        // perDrawDataLayout = MeshShaderPipeline::getPerDrawDataLayout() (set 0 of the shadow pass:
        // b0 perDrawData, b1 instanceTransforms, b2 objects).
        bool init(GPUCullLODPipeline* cullPipeline, vk::DescriptorSetLayout perDrawDataLayout);
        void cleanup();
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // (Re)point the shadow pass set-0 bindings: b0 -> our shadow perDrawData buffer,
        // b1/b2 -> the scene's instance-transform / object buffers (which can change on rebuild).
        void updateSceneBuffers(vk::Buffer instanceTransformBuffer, vk::Buffer objectBuffer);

        // Upload one cull camera per active shadow view (global view order: directional, point,
        // spot — matches ShadowSystem::getActiveShadowViewProjections / getShadowViewIndex). Views
        // beyond SHADOW_CULL_MAX_VIEWS are ignored here (recorder routes them to the legacy path).
        // Returns the number of views that will be GPU-culled this frame.
        uint32_t beginFrame(const std::vector<glm::mat4>& viewProjections, uint32_t objectCount);

        // Reset per-view counters, then dispatch one cull per active view; inserts the barrier so
        // the shadow draw/perDraw buffers are ready for indirect draw + task/mesh shader reads.
        void recordCull(vk::CommandBuffer cmd, uint32_t objectCount);

        [[nodiscard]] bool hasActiveViews() const { return activeViewCount > 0; }
        [[nodiscard]] uint32_t getActiveViewCount() const { return activeViewCount; }

        // Is `globalViewIndex` GPU-culled this frame (i.e. routable to the per-view shadow buffer)?
        [[nodiscard]] bool isViewCulled(uint32_t globalViewIndex) const
        {
            return globalViewIndex < activeViewCount && globalViewIndex < SHADOW_CULL_MAX_VIEWS;
        }

        [[nodiscard]] vk::Buffer getDrawCommandBuffer() const { return drawCommandBuffer; }
        [[nodiscard]] vk::Buffer getDrawCountBuffer() const { return drawCountBuffer; }
        [[nodiscard]] vk::DescriptorSet getPerDrawDataDescSet() const { return shadowPerDrawSet; }

        [[nodiscard]] static uint32_t drawsPerView() { return SHADOW_CULL_DRAWS_PER_VIEW; }
        [[nodiscard]] static uint32_t viewBase(uint32_t slot) { return slot * SHADOW_CULL_DRAWS_PER_VIEW; }
        // Byte offset of `slot`'s counter in the draw-count buffer (one uint per view).
        [[nodiscard]] static vk::DeviceSize countOffset(uint32_t slot) { return slot * sizeof(uint32_t); }

    private:
        bool createBuffers();
        void destroyBuffers();
        static void extractFrustumPlanes(const glm::mat4& viewProjection, glm::vec4 planes[6]);

        core::Device& device;
        GPUCullLODPipeline* cullPipeline = nullptr;
        vk::DescriptorSetLayout perDrawDataLayout;

        vk::Buffer drawCommandBuffer;
        core::VulkanAllocation drawCommandAllocation;
        vk::Buffer perDrawDataBuffer;
        core::VulkanAllocation perDrawDataAllocation;
        vk::Buffer drawCountBuffer;
        core::VulkanAllocation drawCountAllocation;

        vk::Buffer cameraBuffer;                  // host-visible array of GPUCameraData (one per view)
        core::VulkanAllocation cameraAllocation;
        void* cameraMapped = nullptr;
        vk::DeviceSize cameraStride = 0;          // aligned to minUniformBufferOffsetAlignment

        vk::DescriptorPool cullSetPool;           // for the per-view shadow cull sets
        std::vector<vk::DescriptorSet> cullSets;  // SHADOW_CULL_MAX_VIEWS sets (one per region)

        vk::DescriptorPool perDrawSetPool;        // for the shadow pass set-0 variant
        vk::DescriptorSet shadowPerDrawSet;

        uint32_t activeViewCount = 0;
        bool initialized = false;
    };
}
