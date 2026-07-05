#pragma once

#include "../../virtualtexture/VTTypes.hpp"
#include "../../virtualtexture/VTPageTable.hpp"
#include "../../virtualtexture/VTPhysicalPool.hpp"
#include "../../virtualtexture/VTFeedbackReadback.hpp"
#include "../../virtualtexture/VTResidencyCore.hpp"
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <functional>
#include <memory>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
}

// ============================================================================
// Virtual Texturing (VK-1209) — terrain Runtime Virtual Texture manager. Owns the
// VT substrate for a terrain (page table + 2-plane RGBA8 pool + feedback + residency)
// and drives it each frame: decode the previous frame's page requests, plan
// residency (LRU + per-frame budget over the pure VTResidencyCore), map/unmap
// page-table entries, and hand a work-list to the baker. The actual GPU bake (an
// MRT draw of the terrain splat composite into pool tiles) is injected as a BakeFn
// so this orchestration compiles and is reasoned about independently of the
// pipeline. A world-anchored uniform page grid over the terrain XZ AABB (not a
// clipmap) keeps baked pages valid across camera motion. The coarsest mip is pinned
// resident so any lookup resolves — a streaming page reads briefly blurry, not as a
// hole (AC6).
// ============================================================================

namespace render::gpudriven
{
    class TerrainRVTManager
    {
    public:
        struct Config
        {
            uint32_t poolBudgetMB = 128;
            float texelsPerMeter = 8.0f;
            uint32_t pagesPerFrame = 32;
            uint32_t evictionAgeFrames = 60;
        };

        // A scheduled page bake: fill physical `tile` for virtual page `page`, whose world
        // footprint is (minX, minZ, sizeX, sizeZ). The baker sets each page's viewport/scissor
        // from `tile` (pool->getTileViewport/Scissor) and drives the composite over its world rect.
        struct ScheduledBake
        {
            vt::VTPageKey page;
            uint32_t tile = vt::VT_INVALID_TILE;
            glm::vec4 worldRect{0.0f};
        };
        using BakeFn = std::function<void(vk::CommandBuffer, const std::vector<ScheduledBake>&)>;

        // Optional residency gate: given a page's world-XZ rect (minX, minZ, sizeX, sizeZ), return
        // whether loaded terrain covers it. Uncovered pages are left non-resident (the shader falls
        // back to the live composite) so the page budget isn't spent baking fully-black tiles. An
        // empty predicate disables gating (all requested pages are eligible).
        using CoveragePredicate = std::function<bool(const glm::vec4&)>;

        // Per-frame CPU cost of the residency path (VK-1480 instrumentation), split so
        // the feedback memcpy can be told apart from the set-bit decode and the residency
        // planning. Render-thread only; the lead publishes it into the profiler.
        struct FrameCpuStats
        {
            uint64_t readbackUs = 0;   // feedback->readback() (device->host memcpy)
            uint64_t decodeUs = 0;     // set-bit walk + decode + request sort
            uint64_t residencyUs = 0;  // planFrame + evict/allocate + map/unmap
        };

        explicit TerrainRVTManager(core::Device& device);
        ~TerrainRVTManager();

        TerrainRVTManager(const TerrainRVTManager&) = delete;
        TerrainRVTManager& operator=(const TerrainRVTManager&) = delete;

        void init(const Config& cfg, const glm::vec2& worldMin, const glm::vec2& worldMax);
        void cleanup();
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // Frame lifecycle (mirrors VSM's feedback-driven loop):
        void markFeedbackReady() { if (feedback) feedback->markReady(); }
        void beginFrameReadback();                 // decode last frame's feedback into requestedPages
        void updateResidency(uint32_t frame, const CoveragePredicate& covered = {}); // plan + evict + allocate + schedule bakes
        void recordBakes(vk::CommandBuffer cmd, const BakeFn& bake);
        void uploadPageTable(vk::CommandBuffer cmd) { if (pageTable) pageTable->uploadToGPU(cmd); }
        void clearFeedback(vk::CommandBuffer cmd) { if (feedback) feedback->clear(cmd); }
        void copyFeedbackToStaging(vk::CommandBuffer cmd) { if (feedback) feedback->copyToStaging(cmd); }

        // Terrain edits (splat brush, palette change): evict overlapping fine pages so they re-bake.
        void invalidateWorldRect(const glm::vec2& mn, const glm::vec2& mx);

        // Shader-binding accessors.
        [[nodiscard]] vk::Buffer getPageTableBuffer() const { return pageTable ? pageTable->getBuffer() : nullptr; }
        [[nodiscard]] vk::Buffer getFeedbackBuffer() const { return feedback ? feedback->getBuffer() : nullptr; }
        [[nodiscard]] const vt::VTPhysicalPool* getPool() const { return pool.get(); }
        [[nodiscard]] vt::GPUVTImageInfo getImageInfo() const;
        [[nodiscard]] float virtualResTexelsX() const { return static_cast<float>(image.pagesX0 * vt::VT_PAGE_INTERIOR); }

        [[nodiscard]] uint32_t residentPageCount() const { return residency.residentCount(); }
        [[nodiscard]] uint32_t bakesThisFrame() const { return static_cast<uint32_t>(scheduledBakes.size()); }
        [[nodiscard]] float poolUtilization() const { return pool ? pool->utilization() : 0.0f; }
        [[nodiscard]] const FrameCpuStats& lastCpuStats() const { return cpuStats; }

    private:
        void ensureCoarseResident(uint32_t frame);
        void schedule(const vt::VTPageKey& page, uint32_t tile);
        void mapEntry(const vt::VTPageKey& page, uint32_t tile);
        void unmapEntry(const vt::VTPageKey& page);
        [[nodiscard]] uint32_t globalEntryIndex(const vt::VTPageKey& page) const;
        [[nodiscard]] glm::vec4 pageWorldRect(const vt::VTPageKey& page) const;

        core::Device& device;
        Config config;

        glm::vec2 worldMin{0.0f};
        glm::vec2 worldExtent{1.0f};
        vt::VTImageDesc image;
        uint32_t imageId = 0;

        std::unique_ptr<vt::VTPageTable> pageTable;
        std::unique_ptr<vt::VTPhysicalPool> pool;
        std::unique_ptr<vt::VTFeedbackReadback> feedback;
        vt::VTResidencyCore residency;

        std::vector<vt::VTPageKey> requestedPages;
        std::vector<ScheduledBake> scheduledBakes;

        FrameCpuStats cpuStats;

        bool initialized = false;
    };
}
