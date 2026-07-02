#pragma once

#include "../GPUDrivenTypes.hpp"
#include "ShadowBinPacking.hpp"                    // constants + ShadowBinPageRequest + buildPageBinBase
#include "../../../core/RenderManager.hpp"       // core::MAX_FRAMES_IN_FLIGHT
#include "../../../core/VulkanMemoryManager.hpp" // core::VulkanAllocation
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <memory>
#include <cstdint>

namespace core
{
    class Device;
    class Shader;
}

namespace render::gpudriven
{
    // VK-1479 Phase B1: page-binned directional shadow cull.
    //
    // Replaces the legacy "replay the main camera's occlusion-culled draw list once per VSM page"
    // with a per-PAGE GPU cull: one compute dispatch over objects x clipmap-levels frustum/LOD/
    // distance-culls each caster against THAT level, projects its AABB to the overlapped page range
    // (shared ShadowPageOverlap math), and appends a draw command + PerDrawData into fixed per-page
    // bins. The shadow recorder then issues one indirect-count draw per rendering page from its bin.
    //
    // Correctness note (the bug that killed the old "Tier 4"): the dispatch is co-located with the
    // main cull and reads the SAME objectCount + activeIndices buffer, and all host-visible params
    // are ring-buffered per frame-in-flight — so runtime-spawned objects present in the main cull are
    // present here too. Flag default OFF; the legacy path stays the byte-identical fallback.
    //
    // Integration is deliberately no-shader-change: bins hold BOTH a MeshTasksCommand and a
    // (non-deduplicated) PerDrawData at parallel per-page slots, so the existing shadow task/mesh
    // pipeline is reused unchanged — the recorder just binds this binner's draw descriptor set
    // (b0 -> bin PerDrawData, allocated from the shared mesh per-draw layout) and bin command buffer
    // with baseDrawIndex = renderSlot * SHADOW_BIN_CAPACITY. (Dedup via drawRef is a B3 memory win.)

    // One binned shadow VIEW (B2): a directional clipmap level, the spot view, or a point-cube
    // face — all handled uniformly. The bin cull culls/projects each caster against viewProjection
    // and maps overlapped pages to the GLOBAL pageBinBase index gridInfo.z + fy*gridInfo.x + fx.
    // std430; mirror in gpu_cull_shadow_bin.glsl.
    struct alignas(16) ShadowLevelData
    {
        glm::mat4 viewProjection;
        glm::vec4 bias;      // depthBias, slopeBias, normalBias, unused
        glm::uvec4 gridInfo; // pagesX, pagesY, pageBaseOffset (into pageBinBase), unused
    };
    static_assert(sizeof(ShadowLevelData) == 96, "ShadowLevelData must be 96 bytes");

    // Push constants for gpu_cull_shadow_bin.glsl.
    struct ShadowBinPushConstants
    {
        uint32_t objectCount;   // == the main cull's objectCount (stats.totalObjects)
        uint32_t levelCount;
        uint32_t pagesPerLevel; // clipmapPagesPerLevel()
        uint32_t binCapacity;   // SHADOW_BIN_CAPACITY
        uint32_t flags;         // bit0 distanceCull, bit1 occlusionCull (0 in B1), bit2 lodEnabled
        uint32_t pad0;
        uint32_t pad1;
        uint32_t pad2;
    };

    // Read-back diagnostics surfaced in the shadow stats panel (Jira #15141 mandate).
    struct ShadowBinStats
    {
        uint32_t pagesBinned = 0;      // pages that got a bin base this frame
        uint32_t totalBinnedDraws = 0; // sum of per-page counts
        uint32_t overflowedPages = 0;  // pages that hit SHADOW_BIN_CAPACITY
        uint32_t droppedDraws = 0;     // draws dropped to overflow / shadow-draw cap
    };

    class ShadowPageBinner
    {
    public:
        explicit ShadowPageBinner(core::Device& device);
        ~ShadowPageBinner();

        ShadowPageBinner(const ShadowPageBinner&) = delete;
        ShadowPageBinner& operator=(const ShadowPageBinner&) = delete;

        // perDrawLayout: the shared mesh-shader per-draw-data set-0 layout (b0 PerDrawData,
        // b1 InstanceTransform, b2 Object). The binner allocates a compatible draw descriptor set
        // whose b0 points at its own bin PerDrawData buffer.
        void init(vk::DescriptorSetLayout perDrawLayout);
        void cleanup();
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // buildPageBinBase (pure bin-slot accounting) lives in ShadowBinPacking.hpp so the
        // CPU-only Tests project can validate it without this Vulkan-heavy header.

        // ---- Per-frame GPU path --------------------------------------------------------------
        // Stage this frame's per-level VP/bias array + the pageBinBase table (host-visible ring).
        void updateFrameData(const std::vector<ShadowLevelData>& levels,
                             const std::vector<uint32_t>& pageBinBase);

        // Bind the object/camera/activeIndices buffers the cull reads (same handles as main cull).
        void updateComputeDescriptors(vk::Buffer objectBuffer, vk::Buffer cameraBuffer,
                                      vk::Buffer instanceTransformBuffer, vk::Buffer activeIndexBuffer);

        void recordReset(vk::CommandBuffer cmd);   // clear per-page counts + overflow stats
        void recordUpload(vk::CommandBuffer cmd);  // copy ring staging -> device local (pre-barrier)
        void dispatch(vk::CommandBuffer cmd, uint32_t objectCount, uint32_t levelCount,
                      uint32_t pagesPerLevel, uint32_t flags);
        void recordPostBarrier(vk::CommandBuffer cmd); // compute-write -> indirect/task read

        void advanceStagingFrame() { currentStagingFrame = (currentStagingFrame + 1) % core::MAX_FRAMES_IN_FLIGHT; }

        // Recorder-facing: the draw descriptor set (set 0: bin PerDrawData / instance / objects) and
        // the bin command + count buffers. baseDrawIndex for page renderSlot = renderSlot*binCapacity.
        [[nodiscard]] vk::DescriptorSet getDrawDescriptorSet() const { return drawDescriptorSet; }
        [[nodiscard]] vk::Buffer getBinCommandBuffer() const { return binCommandBuffer; }
        [[nodiscard]] vk::Buffer getBinCountBuffer() const { return binCountBuffer; }
        [[nodiscard]] uint32_t getBinCapacity() const { return SHADOW_BIN_CAPACITY; }

        // Latency-tolerant readback of last-ready stats (ring), for the stats panel.
        [[nodiscard]] ShadowBinStats getLastStats() const { return lastStats; }

    private:
        core::Device& device;
        bool initialized = false;

        std::unique_ptr<core::Shader> cullShader;
        vk::Pipeline computePipeline;
        vk::PipelineLayout computePipelineLayout;
        vk::DescriptorSetLayout computeSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet computeDescriptorSet;

        // Draw path (recorder binds this): allocated from the external mesh per-draw layout.
        vk::DescriptorSetLayout drawSetLayout; // = perDrawLayout passed to init (not owned)
        vk::DescriptorSet drawDescriptorSet;

        // Device-local bin arenas.
        vk::Buffer binCommandBuffer;   // MeshTasksCommand[MAX_RENDERED_SHADOW_PAGES*SHADOW_BIN_CAPACITY]
        core::VulkanAllocation binCommandAlloc;
        vk::Buffer binPerDrawBuffer;   // PerDrawData[same]
        core::VulkanAllocation binPerDrawAlloc;
        vk::Buffer binCountBuffer;     // uint[MAX_RENDERED_SHADOW_PAGES]
        core::VulkanAllocation binCountAlloc;
        vk::Buffer overflowBuffer;     // uint[4]: overflowedPages, droppedDraws, ...
        core::VulkanAllocation overflowAlloc;

        // Device-local per-frame inputs (copied from ring staging).
        vk::Buffer levelDataBuffer;    // ShadowLevelData[maxLevels]
        core::VulkanAllocation levelDataAlloc;
        vk::Buffer pageBinBaseBuffer;  // uint[maxPageTableEntries]
        core::VulkanAllocation pageBinBaseAlloc;

        struct StagingFrame
        {
            vk::Buffer levelBuffer;
            core::VulkanAllocation levelAlloc;
            void* levelMapped = nullptr;
            vk::Buffer pageBaseBuffer;
            core::VulkanAllocation pageBaseAlloc;
            void* pageBaseMapped = nullptr;
        };
        std::array<StagingFrame, core::MAX_FRAMES_IN_FLIGHT> stagingFrames{};
        uint32_t currentStagingFrame = 0;

        // Sizes captured this frame for recordUpload copy regions.
        uint32_t stagedLevelCount = 0;
        uint32_t stagedPageCount = 0;
        uint32_t maxPageTableEntries = 0; // capacity of pageBinBaseBuffer

        ShadowBinStats lastStats{};

        void createBuffers();
        void destroyBuffers();
        void createComputePipeline();
        void createDescriptors(vk::DescriptorSetLayout perDrawLayout);
    };
}
