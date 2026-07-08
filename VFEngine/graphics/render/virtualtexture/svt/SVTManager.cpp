#include "SVTManager.hpp"
#include "SVTReadBatch.hpp"
#include "../VTPageExtract.hpp"
#include "../VTFeedbackWords.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "resource/TextureStreamHandle.hpp"
#include "print/Log.hpp"

#include <cstring>
#include <algorithm>
#include <array>
#include <chrono>

namespace render::gpudriven
{
    using namespace render::vt;

    namespace
    {
        constexpr uint32_t kMaxPageTableEntries = 1u << 20; // 4 MB page table (shared by all SVT images)
        constexpr uint32_t kMinSvtDim = 512;                // smaller textures aren't worth paging

        using Clock = std::chrono::steady_clock;
        uint64_t usSince(Clock::time_point t)
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t).count());
        }
    }

    SVTManager::SVTManager(core::Device& device)
        : device(device)
    {
    }

    SVTManager::~SVTManager()
    {
        cleanup();
    }

    void SVTManager::init(const Config& cfg)
    {
        if (initialized)
            return;
        config = cfg;
        // Floor the streaming budget so a bad scene value can't size a zero-length staging ring (a
        // negative JSON value also wraps huge — clamped at the deserialize/UI layer, floored here too).
        if (config.pagesPerFrame == 0u)
            config.pagesPerFrame = 1u;
        if (config.evictionAgeFrames == 0u)
            config.evictionAgeFrames = 1u;
        initialPagesPerFrame = config.pagesPerFrame; // the staging ring is sized from this

        // Two pools (sRGB + Unorm) share one page table + feedback. When linear maps aren't paged the
        // sRGB pool takes the whole budget and the Unorm pool is not created.
        const uint32_t poolCount = config.pageLinearMaps ? 2u : 1u;
        std::array<uint32_t, 2> budgets{config.poolBudgetMB, 0u};
        if (poolCount == 2u)
        {
            const VTPoolBudgetSplit split = vtSplitPoolBudget(config.poolBudgetMB);
            budgets = {split.firstMB, split.secondMB};
        }
        static const std::array<vk::Format, 2> poolFormats{
            vk::Format::eBc7SrgbBlock, vk::Format::eBc7UnormBlock};

        pools.clear();
        pools.resize(poolCount);
        for (uint32_t p = 0; p < poolCount; ++p)
        {
            if (vtPoolBudgetClamped(budgets[p], /*planes*/ 1, /*bytesPerTexel*/ 1))
                vfLogWarning("SVTManager: pool {} budget {}MB exceeds the {}px atlas cap — the excess "
                             "buys no extra tiles", p, budgets[p], VT_MAX_POOL_DIM);

            VTPoolDesc poolDesc;
            poolDesc.poolDim = vtPoolDimForBudget(budgets[p], /*planes*/ 1, /*bytesPerTexel*/ 1); // BC7 ~1 B/texel
            poolDesc.planeFormats = {poolFormats[p]};
            poolDesc.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
            pools[p].pool = std::make_unique<VTPhysicalPool>(device);
            pools[p].pool->init(poolDesc);
            pools[p].residency.clear();
        }

        totalPageTableEntries = kMaxPageTableEntries;
        pageTable = std::make_unique<VTPageTable>(device);
        pageTable->init(totalPageTableEntries);

        feedback = std::make_unique<VTFeedbackReadback>(device);
        feedback->init(totalPageTableEntries);

        tileByteSize = vtTileByteSize(VT_FORMAT_BC7);
        createImageInfoBuffer();

        // Frame-rotated upload staging ring: pagesPerFrame BC7 tiles per frame-in-flight, so an
        // in-flight copy's source is never overwritten (the hazard the single buffer had).
        const vk::DeviceSize stagingSize = static_cast<vk::DeviceSize>(tileByteSize)
                                         * config.pagesPerFrame * core::MAX_FRAMES_IN_FLIGHT;
        core::BufferInfoRequest req(device.getLogicalDevice(), device.getPhysicalDevice(), stagingSize,
                                    vk::BufferUsageFlagBits::eTransferSrc,
                                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        core::BufferUtilities::createBuffer(req, uploadStaging, uploadStagingAllocation, device.getMemoryManager());

        completedQueue = std::make_shared<CompletedQueue>();
        cancelToken = threading::CancellationToken::create();
        epoch = 1;
        currentStagingFrame = 0;

        initialized = true;
        vfLogInfo("SVTManager: {} BC7 pool(s) (budget {}MB), page table {} entries",
                  poolCount, config.poolBudgetMB, totalPageTableEntries);
    }

    void SVTManager::cleanup()
    {
        if (!initialized)
            return;

        // Stop async I/O first: bump epoch so any in-flight result is dropped, cancel unstarted jobs,
        // and wait the running ones (they only touch the shared_ptr handle + completed queue, both of
        // which outlive this via shared_ptr, so waiting is sufficient before destroying GPU resources).
        ++epoch;
        if (cancelToken)
            cancelToken->cancel();
        for (auto& h : inFlightJobs)
            h.wait();
        inFlightJobs.clear();
        inFlight.clear();
        if (completedQueue)
        {
            std::lock_guard<std::mutex> lock(completedQueue->mutex);
            completedQueue->tiles.clear();
            completedQueue->failed.clear();
        }
        pendingPins.clear();
        pinnedKeys.clear();

        const vk::Device vkDevice = device.getLogicalDevice();
        auto& mem = device.getMemoryManager();
        core::BufferUtilities::destroyBuffer(vkDevice, uploadStaging, uploadStagingAllocation, mem);
        core::BufferUtilities::destroyBuffer(vkDevice, imageInfoStaging, imageInfoStagingAllocation, mem);
        core::BufferUtilities::destroyBuffer(vkDevice, imageInfoBuffer, imageInfoAllocation, mem);
        // Any image-info buffers still pending deferred destruction (finding #2): cleanup only runs when
        // the manager is torn down (device idle around the SVT toggle), so freeing them now is safe.
        for (auto& p : pendingImageInfoDestroys)
            core::BufferUtilities::destroyBuffer(vkDevice, p.buffer, p.allocation, mem);
        pendingImageInfoDestroys.clear();
        imageInfoBufferResized = false;
        feedback.reset();
        pageTable.reset();
        pools.clear();
        images.clear();
        pathToImage.clear();
        freeImageIds.clear();
        imageRanges.clear();
        imageInfoCpu.clear();
        requestedPages.clear();
        cpuStats = {};
        initialized = false;
    }

    void SVTManager::createImageInfoBuffer()
    {
        imageInfoCapacity = 1024; // grows lazily if exceeded
        const vk::DeviceSize size = sizeof(GPUVTImageInfo) * imageInfoCapacity;
        core::BufferInfoRequest devReq(device.getLogicalDevice(), device.getPhysicalDevice(), size,
                                       vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                       vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::BufferUtilities::createBuffer(devReq, imageInfoBuffer, imageInfoAllocation, device.getMemoryManager());

        core::BufferInfoRequest stgReq(device.getLogicalDevice(), device.getPhysicalDevice(), size,
                                       vk::BufferUsageFlagBits::eTransferSrc,
                                       vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        core::BufferUtilities::createBuffer(stgReq, imageInfoStaging, imageInfoStagingAllocation, device.getMemoryManager());
    }

    uint32_t SVTManager::registerTexture(const std::string& path, bool srgb)
    {
        if (!initialized)
            return VT_INVALID_TILE;

        if (path.size() < 8 || path.rfind(".vfImage") != path.size() - 8)
            return VT_INVALID_TILE; // SVT only pages .vfImage

        // Route to a pool: sRGB (albedo/emission) -> 0, linear (normal/ORM/height) -> 1. If the Unorm
        // pool wasn't created (pageLinearMaps off) linear maps are refused and stay plain bindless.
        const uint32_t poolId = srgb ? 0u : 1u;
        if (poolId >= pools.size())
            return VT_INVALID_TILE;

        // Dedup within the SAME pool only: the same .vfImage can legitimately back both an sRGB slot
        // and a linear slot, and each needs its own imageId so it samples the correct-format atlas.
        if (auto it = pathToImage.find(path); it != pathToImage.end() && it->second[poolId] != VT_INVALID_TILE)
            return SVT_TAG_BIT | it->second[poolId];

        auto uniqueHandle = resource::TextureStreamResource::openStream(path);
        if (!uniqueHandle)
            return VT_INVALID_TILE;
        std::shared_ptr<resource::TextureStreamHandle> handle(std::move(uniqueHandle));

        const auto& hdr = handle->getHeader();
        if (hdr.compression != resource::TextureCompressionFormat::BC7)
            return VT_INVALID_TILE;
        if (hdr.width < kMinSvtDim || hdr.height < kMinSvtDim || hdr.mipLevels == 0)
            return VT_INVALID_TILE;

        VTImageDesc desc;
        desc.pagesX0 = std::max(1u, (hdr.width + VT_PAGE_INTERIOR - 1u) / VT_PAGE_INTERIOR);
        desc.pagesY0 = std::max(1u, (hdr.height + VT_PAGE_INTERIOR - 1u) / VT_PAGE_INTERIOR);
        desc.mipCount = std::min(vtComputeMipCount(desc.pagesX0, desc.pagesY0), hdr.mipLevels);

        // Refuse images whose coarsest level spans more than the pin cap: pinning them all would
        // monopolize the atlas. Caller falls back to plain (Full) registration.
        const uint32_t pinCount = vtCoarsePinPageCount(desc.pagesX0, desc.pagesY0, desc.mipCount);
        if (pinCount > VT_MAX_PIN_PAGES)
        {
            vfLogWarning("SVTManager: '{}' coarsest mip needs {} pins (> {}), not paged",
                         path, pinCount, VT_MAX_PIN_PAGES);
            return VT_INVALID_TILE;
        }

        const uint32_t base = pageTable->allocateBlock(desc.pagesX0, desc.pagesY0, desc.mipCount);
        if (base == VT_INVALID_TILE)
        {
            vfLogWarning("SVTManager: page table full, '{}' falls back to bindless", path);
            return VT_INVALID_TILE;
        }
        desc.pageTableBase = base;

        SVTImage img;
        img.desc = desc;
        img.poolId = poolId;
        img.fallbackIndex = 0; // default-texture slot until setFallbackIndex
        img.path = path;
        img.handle = std::move(handle);

        // Reuse a tombstoned slot (per-texture reclaim) before growing `images` — imageId is also the
        // SSBO slot and the shader tag, so it must stay stable for every other live image.
        uint32_t imageId;
        if (!freeImageIds.empty())
        {
            imageId = freeImageIds.back();
            freeImageIds.pop_back();
            images[imageId] = std::move(img);
        }
        else
        {
            imageId = static_cast<uint32_t>(images.size());
            images.push_back(std::move(img));
        }

        // Record this path's imageId in its pool slot (the other pool keeps its own registration).
        auto& slots = pathToImage.try_emplace(
            path, std::array<uint32_t, 2>{VT_INVALID_TILE, VT_INVALID_TILE}).first->second;
        slots[poolId] = imageId;

        // Reclaim makes reused bases non-monotonic, so insert the range at its sorted-by-base position —
        // beginFrameReadback's owning-image binary search requires imageRanges stay sorted + disjoint.
        const ImageRange range{base, base + desc.blockEntryCount(), imageId};
        auto rpos = std::upper_bound(imageRanges.begin(), imageRanges.end(), base,
                                     [](uint32_t b, const ImageRange& r) { return b < r.base; });
        imageRanges.insert(rpos, range);

        // Queue the whole coarsest-mip page set as pins (handles truncated-chain multi-page coarse mips).
        const uint32_t coarseMip = desc.mipCount - 1u;
        const uint32_t px = vtPagesAtMip(desc.pagesX0, coarseMip);
        const uint32_t py = vtPagesAtMip(desc.pagesY0, coarseMip);
        for (uint32_t yy = 0; yy < py; ++yy)
            for (uint32_t xx = 0; xx < px; ++xx)
            {
                const VTPageKey k{imageId, coarseMip, xx, yy};
                pinnedKeys.insert(k.packed());
                pendingPins.push_back(k);
            }

        GPUVTImageInfo info;
        info.pagesX0 = desc.pagesX0;
        info.pagesY0 = desc.pagesY0;
        info.mipCount = desc.mipCount;
        info.pageTableBase = desc.pageTableBase;
        info.poolDim = pools[poolId].pool->getPoolDim();
        info.pad0 = pools[poolId].atlasBindlessIndex; // owning pool's atlas slot (repatched in uploadImageInfo)
        info.pad1 = 0;                                 // fallback = default-texture slot until setFallbackIndex
        // imageInfoCpu stays index-parallel with `images`: assign into a reused slot, append a fresh one.
        if (imageId < imageInfoCpu.size())
            imageInfoCpu[imageId] = info;
        else
            imageInfoCpu.push_back(info);
        imageInfoDirty = true;

        return SVT_TAG_BIT | imageId;
    }

    void SVTManager::unregisterImageInternal(uint32_t imageId)
    {
        if (imageId >= images.size())
            return;
        SVTImage& img = images[imageId];
        const uint32_t poolId = img.poolId;

        // Evict this image's resident tiles: free the physical tile + unmap its page-table entry.
        if (poolId < pools.size())
        {
            Pool& P = pools[poolId];
            const VTImageDesc d = img.desc;
            P.residency.evictImage(imageId, [&](const VTPageKey& key, uint32_t tile)
            {
                if (tile != VT_INVALID_TILE)
                    P.pool->freeTile(tile);
                pageTable->unmapEntry(d.pageTableBase
                                      + vtPageLinearIndex(d.pagesX0, d.pagesY0, key.mip, key.x, key.y));
            });
        }

        // Drop its pins, pending pins and in-flight keys so nothing re-requests or strands them.
        std::erase_if(pendingPins, [imageId](const VTPageKey& k) { return k.imageId == imageId; });
        std::erase_if(pinnedKeys, [imageId](uint64_t packed)
                      { return static_cast<uint32_t>((packed >> 40) & 0xFFFFFFu) == imageId; });
        std::erase_if(inFlight, [imageId](uint64_t packed)
                      { return static_cast<uint32_t>((packed >> 40) & 0xFFFFFFu) == imageId; });

        // Return the page-table block and its owning-image range.
        pageTable->freeBlock(img.desc.pageTableBase, img.desc.blockEntryCount());
        std::erase_if(imageRanges, [imageId](const ImageRange& r) { return r.imageId == imageId; });

        // Tombstone the slot (never erase — that would renumber every other image's imageId/tag/SSBO
        // index). The freed slot is reused by a later registerTexture.
        img = SVTImage{};
        if (imageId < imageInfoCpu.size())
            imageInfoCpu[imageId] = GPUVTImageInfo{};
        freeImageIds.push_back(imageId);
        imageInfoDirty = true;
    }

    void SVTManager::unregisterTexture(const std::string& path)
    {
        if (!initialized)
            return;
        auto it = pathToImage.find(path);
        if (it == pathToImage.end())
            return;

        // Bump the epoch first so any in-flight read issued for these (now retiring) registrations is
        // dropped at drain (svtDrainDecision -> DropEpoch) — a late tile must never land in a reused slot.
        ++epoch;

        for (uint32_t poolId = 0; poolId < 2u; ++poolId)
        {
            const uint32_t imageId = it->second[poolId];
            if (imageId != VT_INVALID_TILE)
                unregisterImageInternal(imageId);
        }
        pathToImage.erase(it);
    }

    void SVTManager::setResidencyBudget(uint32_t pagesPerFrame, uint32_t evictionAgeFrames)
    {
        // pagesPerFrame can't exceed the init-time value: the upload staging ring was sized from it, so
        // a larger budget would overrun the ring (svtStagingOffset). evictionAgeFrames applies fully.
        const uint32_t clamped = std::max(1u, pagesPerFrame);
        config.pagesPerFrame = std::min(clamped, initialPagesPerFrame == 0u ? clamped : initialPagesPerFrame);
        config.evictionAgeFrames = std::max(1u, evictionAgeFrames);
    }

    void SVTManager::growImageInfoBuffer()
    {
        if (!needsImageInfoGrow())
            return;

        // Next power-of-two capacity that holds the current image count.
        uint32_t newCapacity = imageInfoCapacity == 0u ? 1024u : imageInfoCapacity;
        while (newCapacity < imageInfoCpu.size())
            newCapacity <<= 1u;

        const vk::DeviceSize size = sizeof(GPUVTImageInfo) * newCapacity;
        vk::Buffer newDev, newStg;
        core::VulkanAllocation newDevAlloc, newStgAlloc;
        core::BufferInfoRequest devReq(device.getLogicalDevice(), device.getPhysicalDevice(), size,
                                       vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                       vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::BufferUtilities::createBuffer(devReq, newDev, newDevAlloc, device.getMemoryManager());
        core::BufferInfoRequest stgReq(device.getLogicalDevice(), device.getPhysicalDevice(), size,
                                       vk::BufferUsageFlagBits::eTransferSrc,
                                       vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        core::BufferUtilities::createBuffer(stgReq, newStg, newStgAlloc, device.getMemoryManager());

        // Defer-destroy the old buffers: the descriptor is UPDATE_AFTER_BIND, so an in-flight frame may
        // still read the old device buffer until its command buffer retires.
        pendingImageInfoDestroys.push_back({imageInfoBuffer, imageInfoAllocation, core::MAX_FRAMES_IN_FLIGHT});
        pendingImageInfoDestroys.push_back({imageInfoStaging, imageInfoStagingAllocation, core::MAX_FRAMES_IN_FLIGHT});

        imageInfoBuffer = newDev;
        imageInfoAllocation = newDevAlloc;
        imageInfoStaging = newStg;
        imageInfoStagingAllocation = newStgAlloc;
        imageInfoCapacity = newCapacity;
        imageInfoDirty = true;          // re-upload the full mirror into the new buffer
        imageInfoBufferResized = true;  // renderer re-wires the pipelines' binding 5 to the new handle
    }

    void SVTManager::setFallbackIndex(uint32_t imageId, uint32_t bindlessSlot)
    {
        if (imageId >= images.size())
            return;
        images[imageId].fallbackIndex = bindlessSlot;
        if (imageId < imageInfoCpu.size())
            imageInfoCpu[imageId].pad1 = bindlessSlot;
        imageInfoDirty = true;
    }

    std::vector<std::string> SVTManager::getRegisteredPaths() const
    {
        // One entry per registered path (a path paged in both pools shares one key); excludes tombstoned
        // slots, whose pathToImage entry was removed on unregister.
        std::vector<std::string> out;
        out.reserve(pathToImage.size());
        for (const auto& [path, slots] : pathToImage)
            out.push_back(path);
        return out;
    }

    void SVTManager::setAtlasBindlessIndex(uint32_t poolId, uint32_t idx)
    {
        if (poolId >= pools.size())
            return;
        pools[poolId].atlasBindlessIndex = idx;
        imageInfoDirty = true;
    }

    void SVTManager::beginFrameReadback()
    {
        requestedPages.clear();
        if (!feedback)
            return;

        const auto tReadback = Clock::now();
        const std::vector<uint32_t>& words = feedback->readback(); // reused buffer (finding #14)
        cpuStats.readbackUs = usSince(tReadback);
        if (words.empty())
            return;

        const auto tDecode = Clock::now();
        const uint32_t wordCount = static_cast<uint32_t>(words.size());
        const uint32_t totalEntries = feedback->getTotalEntries();
        vtForEachSetEntry(words.data(), wordCount, totalEntries, [&](uint32_t entry)
        {
            // Owning image = the block [base, end) containing this global entry (binary search over
            // the base-sorted ranges), replacing the old O(setBits x imageCount) linear scan.
            auto it = std::upper_bound(imageRanges.begin(), imageRanges.end(), entry,
                                       [](uint32_t e, const ImageRange& r) { return e < r.base; });
            if (it == imageRanges.begin())
                return;
            --it;
            if (entry < it->base || entry >= it->end)
                return;
            const VTImageDesc& d = images[it->imageId].desc;
            uint32_t mip = 0, x = 0, y = 0;
            if (vtDecodeEntry(d.pagesX0, d.pagesY0, d.mipCount, entry - it->base, mip, x, y))
                requestedPages.push_back(VTPageKey{it->imageId, mip, x, y});
        });

        // Finer mips first: under the per-frame budget the visible detail wins.
        std::sort(requestedPages.begin(), requestedPages.end(),
                  [](const VTPageKey& a, const VTPageKey& b) { return a.mip < b.mip; });
        cpuStats.decodeUs = usSince(tDecode);
    }

    void SVTManager::updateAndUpload(vk::CommandBuffer cmd, uint32_t frame)
    {
        if (!initialized)
            return;

        // Rotate the staging ring so this frame's copies don't overwrite an in-flight source.
        currentStagingFrame = (currentStagingFrame + 1u) % core::MAX_FRAMES_IN_FLIGHT;

        // Free image-info buffers retired by an earlier grow once MAX_FRAMES_IN_FLIGHT frames have passed
        // (ticked before this frame's grow so a buffer retired now gets the full grace period).
        if (!pendingImageInfoDestroys.empty())
        {
            const vk::Device vkDevice = device.getLogicalDevice();
            auto& mem = device.getMemoryManager();
            std::erase_if(pendingImageInfoDestroys, [&](PendingImageInfoDestroy& p)
            {
                if (p.framesLeft > 0u)
                    --p.framesLeft;
                if (p.framesLeft == 0u)
                {
                    core::BufferUtilities::destroyBuffer(vkDevice, p.buffer, p.allocation, mem);
                    return true;
                }
                return false;
            });
        }

        // Grow the image-info SSBO before it's read this frame if registration outran its capacity
        // (finding #2). The new buffer is uploaded below; the renderer re-wires binding 5 on consuming
        // imageInfoBufferResized. Old buffers linger on the deferred-destroy ring for in-flight frames.
        if (needsImageInfoGrow())
            growImageInfoBuffer();

        // --- Phase 1: drain async-produced tiles onto the GPU (budget: pagesPerFrame/frame) ---
        const auto tDrain = Clock::now();
        std::vector<CompletedTile> drained;
        std::vector<uint64_t> drainedFailed;
        {
            std::lock_guard<std::mutex> lock(completedQueue->mutex);
            drained.swap(completedQueue->tiles);
            drainedFailed.swap(completedQueue->failed);
        }
        // Failed reads: erase the stranded in-flight keys so pushReq can re-request the page next frame
        // (finding #6 — otherwise a transient failure leaves the region stuck on the coarse fallback).
        for (uint64_t k : drainedFailed)
            inFlight.erase(k);

        std::vector<std::vector<vk::BufferImageCopy>> poolCopies(pools.size());
        std::vector<CompletedTile> requeue;
        uint32_t slot = 0;
        for (auto& ct : drained)
        {
            if (slot >= config.pagesPerFrame)
            {
                // Over budget this frame: defer to a later frame. Keep the key in `inFlight` so the
                // submit phase doesn't re-issue a read for a tile already sitting in the queue.
                requeue.push_back(std::move(ct));
                continue;
            }
            inFlight.erase(ct.key.packed()); // consumed this frame (accepted or dropped)

            if (ct.poolId >= pools.size())
                continue;

            Pool& P = pools[ct.poolId];
            const bool resident = P.residency.isResident(ct.key);
            const SVTDrainDecision d = svtDrainDecision(ct.epoch, epoch, ct.key.imageId,
                                                        static_cast<uint32_t>(images.size()), resident);
            if (d != SVTDrainDecision::Accept)
                continue;

            const uint32_t tile = P.pool->allocateTile();
            if (tile == VT_INVALID_TILE)
                continue; // pool full: drop this tile; feedback re-requests it later

            const uint64_t off = svtStagingOffset(currentStagingFrame, slot, config.pagesPerFrame, tileByteSize);
            std::memcpy(static_cast<uint8_t*>(uploadStagingAllocation.mappedPtr) + off,
                        ct.bytes.data(), std::min<size_t>(ct.bytes.size(), tileByteSize));
            poolCopies[ct.poolId].push_back(
                P.pool->getTileBufferCopy(tile, 0, static_cast<vk::DeviceSize>(off)));

            const VTImageDesc& dsc = images[ct.key.imageId].desc;
            const uint32_t per = P.pool->getTilesPerSide();
            pageTable->mapEntry(dsc.pageTableBase
                                + vtPageLinearIndex(dsc.pagesX0, dsc.pagesY0, ct.key.mip, ct.key.x, ct.key.y),
                                tile % per, tile / per);
            const bool pinned = pinnedKeys.count(ct.key.packed()) != 0;
            P.residency.commitAllocation(ct.key, tile, frame, pinned);
            ++slot;
        }
        if (!requeue.empty())
        {
            std::lock_guard<std::mutex> lock(completedQueue->mutex);
            for (auto& ct : requeue)
                completedQueue->tiles.push_back(std::move(ct));
        }

        for (uint32_t p = 0; p < pools.size(); ++p)
        {
            if (poolCopies[p].empty())
                continue;
            Pool& P = pools[p];
            P.pool->transition(cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal,
                               vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer,
                               vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eTransferWrite);
            cmd.copyBufferToImage(uploadStaging, P.pool->planeImage(0), vk::ImageLayout::eTransferDstOptimal,
                                  static_cast<uint32_t>(poolCopies[p].size()), poolCopies[p].data());
            P.pool->transition(cmd, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                               vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                               vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead);
        }
        cpuStats.drainUploadUs = usSince(tDrain);

        // --- Phase 2: plan evictions + submit new read jobs ---
        const auto tSubmit = Clock::now();
        submitReadJobs(frame);
        cpuStats.submitUs = usSince(tSubmit);

        pageTable->uploadToGPU(cmd);
        uploadImageInfo(cmd);
    }

    void SVTManager::submitReadJobs(uint32_t frame)
    {
        // Reap finished jobs (drives nothing but keeps the vector bounded).
        std::erase_if(inFlightJobs, [](const threading::JobHandle& h) { return h.isComplete(); });

        // Drop pins that are now resident so they stop being re-requested.
        std::erase_if(pendingPins, [&](const VTPageKey& k)
        {
            if (k.imageId >= images.size())
                return true;
            return pools[images[k.imageId].poolId].residency.isResident(k);
        });

        // A job in flight will consume a tile in its pool when it drains — discount those so the
        // eviction planner doesn't over-commit.
        std::vector<uint32_t> inflightPerPool(pools.size(), 0u);
        for (uint64_t packed : inFlight)
        {
            const uint32_t iid = static_cast<uint32_t>((packed >> 40) & 0xFFFFFFu);
            if (iid < images.size())
            {
                const uint32_t pid = images[iid].poolId;
                if (pid < inflightPerPool.size())
                    ++inflightPerPool[pid];
            }
        }

        // Per-pool request lists (pins first for priority, then feedback), skipping in-flight pages.
        std::vector<std::vector<VTPageKey>> poolReq(pools.size());
        auto pushReq = [&](const VTPageKey& k)
        {
            if (k.imageId >= images.size())
                return;
            if (inFlight.count(k.packed()))
                return;
            poolReq[images[k.imageId].poolId].push_back(k);
        };
        for (const auto& k : pendingPins)
            pushReq(k);
        for (const auto& k : requestedPages)
            pushReq(k);

        // Per pool: plan (touch resident, decide evictions + misses); execute evictions now.
        std::vector<VTPageKey> toSubmit;
        for (uint32_t p = 0; p < pools.size(); ++p)
        {
            Pool& P = pools[p];
            const uint32_t freeTiles = P.pool->freeTileCount();
            const uint32_t freeForPlan = freeTiles > inflightPerPool[p] ? freeTiles - inflightPerPool[p] : 0u;
            const VTResidencyPlan plan = P.residency.planFrame(poolReq[p], frame, freeForPlan,
                                                               config.pagesPerFrame, config.evictionAgeFrames);
            for (const auto& e : plan.toEvict)
            {
                const uint32_t tile = P.residency.commitEviction(e);
                if (tile != VT_INVALID_TILE)
                {
                    P.pool->freeTile(tile);
                    if (e.imageId < images.size())
                    {
                        const VTImageDesc& d = images[e.imageId].desc;
                        pageTable->unmapEntry(d.pageTableBase
                                              + vtPageLinearIndex(d.pagesX0, d.pagesY0, e.mip, e.x, e.y));
                    }
                }
            }
            for (const auto& a : plan.toAllocate)
                toSubmit.push_back(a);
        }

        // Coalesce reads by (imageId, mip) within the global issue budget; one worker job per group.
        const std::vector<SVTReadGroup> groups = svtBuildReadGroups(toSubmit, config.pagesPerFrame);
        for (const auto& g : groups)
        {
            if (g.imageId >= images.size())
                continue;
            std::shared_ptr<resource::TextureStreamHandle> handle = images[g.imageId].handle;
            const uint32_t poolId = images[g.imageId].poolId;
            const uint32_t mip = g.mip;
            const uint64_t ep = epoch;
            std::shared_ptr<CompletedQueue> cq = completedQueue;
            std::vector<VTPageKey> pages = g.pages;
            for (const auto& k : pages)
                inFlight.insert(k.packed());

            threading::JobHandle h = threading::JobSystem::instance().submitJob(
                [handle, poolId, mip, pages, ep, cq]()
                {
                    // Worker thread: read the mip once, extract every requested page, push results.
                    // Never touches `this`, `images`, the pools, or the page table. Every page that does
                    // NOT yield a tile is reported failed so the render thread clears it from `inFlight`
                    // (finding #6) — otherwise a failed read strands the key and the page never re-streams.
                    std::vector<CompletedTile> local;
                    std::vector<uint64_t> failed;
                    local.reserve(pages.size());
                    if (handle)
                    {
                        try
                        {
                            resource::MipLevelData mipData;
                            if (handle->readMipLevel(mip, mipData))
                            {
                                for (const auto& key : pages)
                                {
                                    std::vector<uint8_t> tile;
                                    if (vtExtractTile(mipData.data.data(), static_cast<uint32_t>(mipData.data.size()),
                                                      mipData.width, mipData.height, VT_FORMAT_BC7, key.x, key.y, tile))
                                    {
                                        CompletedTile ct;
                                        ct.key = key;
                                        ct.poolId = poolId;
                                        ct.epoch = ep;
                                        ct.bytes = std::move(tile);
                                        local.push_back(std::move(ct));
                                    }
                                    else
                                    {
                                        failed.push_back(key.packed());
                                    }
                                }
                            }
                            else
                            {
                                for (const auto& key : pages)
                                    failed.push_back(key.packed());
                            }
                        }
                        catch (const std::exception& e)
                        {
                            vfLogError("SVTManager: async tile read failed: {}", e.what());
                            // Discard any partial results and mark the whole group failed for re-request.
                            local.clear();
                            failed.clear();
                            for (const auto& key : pages)
                                failed.push_back(key.packed());
                        }
                    }
                    else
                    {
                        for (const auto& key : pages)
                            failed.push_back(key.packed());
                    }

                    if (!cq)
                        return;
                    std::lock_guard<std::mutex> lock(cq->mutex);
                    for (auto& t : local)
                        cq->tiles.push_back(std::move(t));
                    for (uint64_t k : failed)
                        cq->failed.push_back(k);
                },
                cancelToken, threading::JobPriority::NORMAL);
            inFlightJobs.push_back(std::move(h));
        }
    }

    void SVTManager::uploadImageInfo(vk::CommandBuffer cmd)
    {
        if (!imageInfoDirty || imageInfoCpu.empty())
            return;
        // Keep each image's owning-pool atlas slot + pool dim current (the atlas slots may be assigned
        // after some textures were already registered).
        for (uint32_t i = 0; i < imageInfoCpu.size() && i < images.size(); ++i)
        {
            const uint32_t poolId = images[i].poolId;
            if (poolId < pools.size())
            {
                imageInfoCpu[i].pad0 = pools[poolId].atlasBindlessIndex;
                imageInfoCpu[i].poolDim = pools[poolId].pool->getPoolDim();
            }
        }

        // updateAndUpload grows the buffer before this runs, so count == size in the common path; the
        // clamp is a safety net that never lets the memcpy overrun the staging buffer.
        const size_t count = std::min<size_t>(imageInfoCpu.size(), imageInfoCapacity);
        const vk::DeviceSize bytes = static_cast<vk::DeviceSize>(count) * sizeof(GPUVTImageInfo);
        std::memcpy(imageInfoStagingAllocation.mappedPtr, imageInfoCpu.data(), bytes);

        vk::BufferCopy copy{};
        copy.size = bytes;
        cmd.copyBuffer(imageInfoStaging, imageInfoBuffer, 1, &copy);

        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = imageInfoBuffer;
        barrier.offset = 0;
        barrier.size = bytes;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                            {}, 0, nullptr, 1, &barrier, 0, nullptr);

        // Stay dirty if the mirror still outgrows capacity (a grow next frame completes the upload).
        imageInfoDirty = (count < imageInfoCpu.size());
    }

    size_t SVTManager::poolAtlasBytes(uint32_t poolId) const
    {
        if (poolId >= pools.size() || !pools[poolId].pool)
            return 0;
        // One BC7 plane: poolDim^2 texels at ~1 B/texel (BC7 = 8 bpp = 1 B/texel).
        const uint64_t dim = pools[poolId].pool->getPoolDim();
        return static_cast<size_t>(dim * dim);
    }

    uint32_t SVTManager::residentPageCount() const
    {
        uint32_t total = 0;
        for (const auto& P : pools)
            total += P.residency.residentCount();
        return total;
    }
}
