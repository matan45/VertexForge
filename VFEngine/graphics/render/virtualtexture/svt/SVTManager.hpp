#pragma once

#include "../VTTypes.hpp"
#include "../VTPageTable.hpp"
#include "../VTPhysicalPool.hpp"
#include "../VTFeedbackReadback.hpp"
#include "../VTResidencyCore.hpp"
#include "../../../core/VulkanMemoryManager.hpp"
#include "../../../core/GraphicsConstants.hpp"
#include "../../../core/BindlessConstants.hpp"
#include "threading/JobSystem.hpp"
#include "threading/CancellationToken.hpp"
#include "memory/VramAssetSnapshot.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <string>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <cstdint>

namespace core
{
    class Device;
}
namespace resource
{
    class TextureStreamHandle;
}

// ============================================================================
// Virtual Texturing (VK-1209 Phase 2 + VK-1480) — Streamed Virtual Textures for
// material textures. Two BC7 physical page atlases (sRGB for albedo/emission,
// Unorm for normal/ORM/height — VK-1480 Phase 6) over ONE shared page table +
// feedback + per-pool residency (the shared VT substrate). Pages are extracted
// from the existing .vfImage per-mip data (no format change) by WORKER JOBS
// (VK-1480 Phase 4 async I/O) and uploaded on the render thread from a
// frame-rotated staging ring. A registered texture gets a bit-31-tagged index the
// mesh shader resolves through this system's page table (SVT_TAG | imageId); its
// coarsest mip is PINNED resident so lookups always resolve, and the whole-image
// fallback shrinks to a tiny tail-only streamer image (VK-1480 Phase 5) so SVT
// now saves VRAM instead of adding it. Default OFF.
// ============================================================================

namespace render::gpudriven
{
    // Bit 31 tags a value in PerDrawData.textureIndices as SVT-backed (bindless indices only
    // ever use the low 12 bits — the mesh shader rejects >= 4096 — so bit 31 is free).
    inline constexpr uint32_t SVT_TAG_BIT = 0x80000000u;

    // VK-1482: INVALID_TEXTURE_INDEX (0xFFFFFFFF) also has bit 31 set — an unbound/sentinel slot
    // must never classify as SVT-tagged. GLSL mirror: isSVTTagged() in
    // resources/shaders/gpudriven/mesh_shader_gpudriven.glsl (parity by convention, like
    // vt_types.glsl <-> VTTypes.hpp).
    inline constexpr bool svtIsTaggedIndex(uint32_t index) noexcept
    {
        return index != core::INVALID_TEXTURE_INDEX && (index & SVT_TAG_BIT) != 0u;
    }
    static_assert(!svtIsTaggedIndex(core::INVALID_TEXTURE_INDEX),
                  "VK-1482: the unbound sentinel must not classify as SVT-tagged");
    static_assert(svtIsTaggedIndex(SVT_TAG_BIT | 0u) && svtIsTaggedIndex(SVT_TAG_BIT | 4095u),
                  "genuine SVT tags must classify as tagged");
    static_assert(!svtIsTaggedIndex(0u) && !svtIsTaggedIndex(0xFFu) && !svtIsTaggedIndex(4095u),
                  "bindless indices and the legacy 0xFF sentinel must not classify as SVT-tagged");

    class SVTManager
    {
    public:
        struct Config
        {
            uint32_t poolBudgetMB = 256;   // total budget across pools (split 50/50 when linear maps page)
            uint32_t pagesPerFrame = 32;
            uint32_t evictionAgeFrames = 60;
            // VK-1480: also page linear (Unorm) maps through a second atlas. Off = one sRGB pool only
            // and the whole budget feeds it; linear maps stay plain bindless (caller-gated).
            bool pageLinearMaps = true;
            // VK-1480 debug escape hatch: keep the whole-image bindless fallback resident (register the
            // stream texture Full instead of TailOnly). Off = the VRAM-saving tail-only path.
            bool keepFullFallback = false;
        };

        // Per-frame CPU cost split (chrono µs), published through the profiler at integration.
        struct FrameCpuStats
        {
            uint64_t readbackUs = 0;   // feedback staging memcpy
            uint64_t decodeUs = 0;     // set-bit -> page decode (binary search)
            uint64_t drainUploadUs = 0;// drain async tiles + record GPU copies
            uint64_t submitUs = 0;     // plan evictions + submit read jobs
        };

        explicit SVTManager(core::Device& device);
        ~SVTManager();

        SVTManager(const SVTManager&) = delete;
        SVTManager& operator=(const SVTManager&) = delete;

        void init(const Config& cfg);
        void cleanup();
        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] bool keepFullFallback() const { return config.keepFullFallback; }

        // Register a .vfImage as SVT-backed. srgb picks the owning atlas (sRGB vs Unorm). Returns
        // SVT_TAG_BIT | imageId, or vt::VT_INVALID_TILE if it can't be paged (not .vfImage / not BC7 /
        // too small / coarse-tail pin set too large / page table full / linear pool absent). The
        // whole-image fallback slot is supplied separately via setFallbackIndex (Phase 5 reorder:
        // SVT registration happens before the fallback image exists).
        uint32_t registerTexture(const std::string& path, bool srgb);

        // Release every SVT registration of `path` (both the sRGB and Unorm pool, if it was registered
        // in both): evict its resident tiles, free its page-table block and physical tiles, and tombstone
        // its imageId slot for reuse. Call from the renderer's texture-release path so streaming scenes
        // don't leak page-table entries until the shared table exhausts. No-op if `path` isn't registered.
        void unregisterTexture(const std::string& path);

        // Point image `imageId`'s whole-image fallback (pad1) at a bindless slot. Until set it stays
        // 0 = the default-texture slot (a safe sentinel — the coarse pin makes real content resolve).
        void setFallbackIndex(uint32_t imageId, uint32_t bindlessSlot);

        // VK-1209 live settings: update the per-frame page budget + eviction age on a running manager.
        // pagesPerFrame is clamped to the init-time value because the upload staging ring was sized from
        // it; evictionAgeFrames applies fully. Both floored at 1.
        void setResidencyBudget(uint32_t pagesPerFrame, uint32_t evictionAgeFrames);

        // VK-1209 image-info SSBO growth (finding #2): registering more images than the SSBO holds needs
        // a larger device buffer + a descriptor rebind on the consumers. The manager can't rebind the
        // pipelines itself, so the renderer polls needsImageInfoGrow() at a safe point, calls
        // growImageInfoBuffer(), then re-wires the pipelines when consumeImageInfoBufferResized() is set.
        [[nodiscard]] bool needsImageInfoGrow() const
        {
            return imageInfoCpu.size() > imageInfoCapacity;
        }
        void growImageInfoBuffer();
        bool consumeImageInfoBufferResized()
        {
            const bool r = imageInfoBufferResized;
            imageInfoBufferResized = false;
            return r;
        }

        // Paths of every SVT-registered image (for promoteToFull on SVT disable).
        [[nodiscard]] std::vector<std::string> getRegisteredPaths() const;

        // Frame lifecycle (mirrors the RVT loop; upload replaces bake).
        void markFeedbackReady() { if (feedback) feedback->markReady(); }
        void beginFrameReadback();
        void updateAndUpload(vk::CommandBuffer cmd, uint32_t frame); // drain async tiles + plan + submit + page-table
        void clearFeedback(vk::CommandBuffer cmd) { if (feedback) feedback->clear(cmd); }
        void copyFeedbackToStaging(vk::CommandBuffer cmd) { if (feedback) feedback->copyToStaging(cmd); }

        // Each pool's atlas is registered once in the bindless heap (UE5-style); its slot is baked into
        // every owning image's info (pad0) so the shader samples bindlessTextures[atlasIndex].
        void setAtlasBindlessIndex(uint32_t poolId, uint32_t idx);

        // Shader binding.
        [[nodiscard]] vk::Buffer getPageTableBuffer() const { return pageTable ? pageTable->getBuffer() : nullptr; }
        [[nodiscard]] vk::Buffer getFeedbackBuffer() const { return feedback ? feedback->getBuffer() : nullptr; }
        [[nodiscard]] vk::Buffer getImageInfoBuffer() const { return imageInfoBuffer; }
        [[nodiscard]] uint32_t poolCount() const { return static_cast<uint32_t>(pools.size()); }
        [[nodiscard]] const vt::VTPhysicalPool* getPool(uint32_t poolId) const
        {
            return poolId < pools.size() ? pools[poolId].pool.get() : nullptr;
        }
        [[nodiscard]] uint32_t imageCount() const { return static_cast<uint32_t>(images.size()); }

        // Stats (surfaced through the profiler's CPU rows at integration).
        [[nodiscard]] const FrameCpuStats& lastCpuStats() const { return cpuStats; }
        [[nodiscard]] size_t poolAtlasBytes(uint32_t poolId) const;
        [[nodiscard]] uint32_t residentPageCount() const;
        [[nodiscard]] uint32_t pinnedPageCount() const { return static_cast<uint32_t>(pinnedKeys.size()); }
        [[nodiscard]] uint32_t pinBacklog() const { return static_cast<uint32_t>(pendingPins.size()); }

        // VK-1539 memory profiler: append one per-owner VRAM row {image path, resident pages ×
        // tileByteSize} for each SVT image with resident atlas pages, accumulating vtTotal. Walks
        // every pool's residency, so it MUST be called on the render thread that owns it. Note the
        // atlas is a shared fixed-budget pool — these rows are an occupancy share, not distinct
        // allocations, and reconcile internally only (no CullingStats counterpart).
        void appendVramRows(std::vector<memory::VramAssetRow>& out, uint64_t& vtTotal) const;

    private:
        struct SVTImage
        {
            vt::VTImageDesc desc;
            uint32_t poolId = 0;
            uint32_t fallbackIndex = 0; // 0 = default-texture slot sentinel until setFallbackIndex
            std::string path;
            // shared_ptr so an in-flight worker job keeps the handle alive even if `images` is cleared.
            std::shared_ptr<resource::TextureStreamHandle> handle;
        };

        // One physical atlas + its own tile residency + its bindless slot. Pool 0 = sRGB, 1 = Unorm.
        struct Pool
        {
            std::unique_ptr<vt::VTPhysicalPool> pool;
            vt::VTResidencyCore residency;
            uint32_t atlasBindlessIndex = 0;
        };

        // A tile a worker job extracted from disk, waiting for the render thread to upload it.
        struct CompletedTile
        {
            vt::VTPageKey key;
            uint32_t poolId = 0;
            uint64_t epoch = 0;
            std::vector<uint8_t> bytes;
        };
        // Shared with worker jobs by shared_ptr so it outlives the manager if a job is mid-push.
        struct CompletedQueue
        {
            std::mutex mutex;
            std::vector<CompletedTile> tiles;
            // Packed keys the worker could not produce a tile for (failed read / extract). The drain
            // loop erases these from `inFlight` so the page can be re-requested (finding #6: otherwise a
            // transient read failure strands the key in-flight forever and the region stays blurry).
            std::vector<uint64_t> failed;
        };

        // Fast owning-image lookup: [base, end) -> imageId, kept sorted by base. With per-texture
        // reclaim (finding #9) a reused block's base is no longer monotonic, so registerTexture inserts
        // in sorted position rather than push_back — the beginFrameReadback binary search relies on it.
        struct ImageRange
        {
            uint32_t base;
            uint32_t end;
            uint32_t imageId;
        };

        // Old image-info buffers awaiting destruction after growth (finding #2): the descriptor is
        // UPDATE_AFTER_BIND, so an in-flight frame may still read the previous buffer — keep it alive
        // MAX_FRAMES_IN_FLIGHT more frames before freeing.
        struct PendingImageInfoDestroy
        {
            vk::Buffer buffer;
            core::VulkanAllocation allocation;
            uint32_t framesLeft = 0;
        };

        void createImageInfoBuffer();
        void uploadImageInfo(vk::CommandBuffer cmd);
        void submitReadJobs(uint32_t frame); // group planFrame output + submit worker jobs

        // Reclaim one image slot (evict tiles, free the page-table block, tombstone). Does NOT touch
        // pathToImage — the public unregisterTexture owns that so it can clear both pool entries.
        void unregisterImageInternal(uint32_t imageId);

        core::Device& device;
        Config config;

        std::unique_ptr<vt::VTPageTable> pageTable;
        std::unique_ptr<vt::VTFeedbackReadback> feedback;
        std::vector<Pool> pools;

        std::vector<SVTImage> images;
        // path -> imageId per pool ([0]=sRGB, [1]=Unorm), VT_INVALID_TILE = not registered in that pool.
        // Pool-aware so the same .vfImage used as both an sRGB albedo and a linear normal/ORM map gets a
        // separate imageId per atlas (finding #11) instead of aliasing to the first-registered pool.
        std::unordered_map<std::string, std::array<uint32_t, 2>> pathToImage;
        std::vector<uint32_t> freeImageIds; // tombstoned slots (reclaim), reused before growing `images`
        std::vector<ImageRange> imageRanges;

        // GPUVTImageInfo[] SSBO (one per registered image) + CPU mirror. Device-local (read per
        // material fragment) with a host-visible staging buffer for the upload copy.
        vk::Buffer imageInfoBuffer;
        core::VulkanAllocation imageInfoAllocation;
        vk::Buffer imageInfoStaging;
        core::VulkanAllocation imageInfoStagingAllocation;
        std::vector<vt::GPUVTImageInfo> imageInfoCpu;
        bool imageInfoDirty = false;
        uint32_t imageInfoCapacity = 0;
        bool imageInfoBufferResized = false; // set on grow; renderer re-wires the pipelines on consume
        std::vector<PendingImageInfoDestroy> pendingImageInfoDestroys;

        // Frame-rotated upload staging ring: pagesPerFrame BC7 tiles per frame-in-flight.
        vk::Buffer uploadStaging;
        core::VulkanAllocation uploadStagingAllocation;
        uint32_t tileByteSize = 0;
        uint32_t currentStagingFrame = 0;
        // The staging ring is sized from pagesPerFrame at init; a live budget change (setResidencyBudget)
        // may not raise the effective pagesPerFrame above this without overrunning the ring.
        uint32_t initialPagesPerFrame = 0;

        // Async I/O (Phase 4).
        std::shared_ptr<CompletedQueue> completedQueue;
        std::unordered_set<uint64_t> inFlight;          // packed page keys currently being read
        std::vector<threading::JobHandle> inFlightJobs; // reaped each frame; waited in cleanup
        threading::CancellationToken::Ptr cancelToken;  // cancels unstarted jobs on teardown
        uint64_t epoch = 1;                             // bumped on cleanup; stale results dropped at drain

        // Coarse-tail pinning (Phase 5).
        std::unordered_set<uint64_t> pinnedKeys;  // pages committed pinned=true (never evicted)
        std::vector<vt::VTPageKey> pendingPins;   // pins not yet resident (re-requested each frame)

        std::vector<vt::VTPageKey> requestedPages; // this frame's feedback requests (decoded)
        uint32_t totalPageTableEntries = 0;
        FrameCpuStats cpuStats;
        bool initialized = false;
    };
}
