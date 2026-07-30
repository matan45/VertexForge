#include "TerrainRVTManager.hpp"
#include "TerrainRVTLayout.hpp"
#include "terrain/TerrainRVTBudget.hpp" // VK-1610: shared page-capacity math + the warn threshold
#include "../../virtualtexture/VTFeedbackWords.hpp"
#include "../../../core/Device.hpp"
#include "print/Log.hpp"

#include <algorithm>
#include <chrono>

namespace render::gpudriven
{
    using namespace render::vt;

    namespace
    {
        // Bound mip-0 pages per side so the page table (sum of the pyramid) and the 12-bit
        // page-coord field stay reasonable. 1024 pages * 120 interior texels ~= 122k virtual
        // texels/side; the pyramid is ~1.4M entries (~5.6 MB page table).
        constexpr uint32_t kMaxPagesPerSide = 1024;
    }

    TerrainRVTManager::TerrainRVTManager(core::Device& device)
        : device(device)
    {
    }

    TerrainRVTManager::~TerrainRVTManager()
    {
        cleanup();
    }

    void TerrainRVTManager::init(const Config& cfg, const glm::vec2& wMin, const glm::vec2& wMax)
    {
        if (initialized)
            return;

        config = cfg;
        worldMin = wMin;
        worldExtent = glm::max(wMax - wMin, glm::vec2(1.0f));

        const float density = config.texelsPerMeter > 0.0f ? config.texelsPerMeter : 8.0f;
        auto pagesForExtent = [&](float extentMeters) -> uint32_t
        {
            const float texels = extentMeters * density;
            uint32_t pages = static_cast<uint32_t>(std::ceil(texels / static_cast<float>(VT_PAGE_INTERIOR)));
            pages = std::clamp(pages, 1u, kMaxPagesPerSide);
            return pages;
        };

        image.pagesX0 = pagesForExtent(worldExtent.x);
        image.pagesY0 = pagesForExtent(worldExtent.y);
        image.computeMips();
        image.pageTableBase = 0;
        imageId = 0;

        // Budget the heterogeneous plane set by its aggregate bytes/texel (8 B legacy,
        // 20 B with RGBA8 normal + RGBA16F emission).
        const TerrainRVTLayout layout = terrainRVTLayout(config.detailMaps);
        const uint32_t poolDim = vtPoolDimForBudget(
            config.poolBudgetMB, /*planes*/ 1, layout.bytesPerTexel);
        VTPoolDesc poolDesc;
        poolDesc.poolDim = poolDim;
        poolDesc.planeFormats = layout.planeFormats;
        // eTransferDst so init can seed the atlas to zero (ORM alpha 0 = "uncovered"); the
        // shader treats an uncovered/unbaked texel as a composite fallback, never black.
        poolDesc.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled
                       | vk::ImageUsageFlagBits::eTransferDst;
        poolDesc.enableAnisotropy = false;

        pool = std::make_unique<VTPhysicalPool>(device);
        pool->init(poolDesc);

        const uint32_t totalEntries = image.blockEntryCount();
        pageTable = std::make_unique<VTPageTable>(device);
        pageTable->init(totalEntries);
        const uint32_t base = pageTable->allocateBlock(image.pagesX0, image.pagesY0, image.mipCount);
        image.pageTableBase = base == VT_INVALID_TILE ? 0u : base; // single image -> base 0

        feedback = std::make_unique<VTFeedbackReadback>(device);
        feedback->init(totalEntries);

        residency.clear();
        requestedPages.clear();
        scheduledBakes.clear();

        initialized = true;
        vfLogInfo("TerrainRVTManager: {}x{} mip-0 pages, {} mips, pool {}x{} ({} tiles, {} planes @ {} B/texel "
                  "from {} MB), world {:.0f}x{:.0f} m @ {:.1f} texels/m",
                  image.pagesX0, image.pagesY0, image.mipCount, poolDim, poolDim,
                  pool->maxTiles(), layout.planeFormats.size(), layout.bytesPerTexel,
                  config.poolBudgetMB, worldExtent.x, worldExtent.y, density);

        // VK-1610: the MB budget is stable across layouts but the PAGE COUNT it buys is not - the
        // 4-plane detail layout costs 20 B/texel against the legacy 8, so the same 128 MB drops
        // from 1024 tiles to 400. A pool too small to hold the camera's footprint evicts and
        // re-bakes every frame, and the only symptom is GPU time inside the bake scope. Say so at
        // init rather than leaving it to be discovered as "the terrain got slower".
        if (pool->maxTiles() < ::terrain::TERRAIN_RVT_MIN_HEALTHY_PAGES)
        {
            vfLogWarning("TerrainRVTManager: only {} resident pages fit in {} MB at {} B/texel. Expect "
                         "page thrash (constant re-bakes) as the camera moves; raise the RVT pool budget "
                         "under Render Config > Virtual Texturing.",
                         pool->maxTiles(), config.poolBudgetMB, layout.bytesPerTexel);
        }
    }

    void TerrainRVTManager::cleanup()
    {
        if (!initialized)
            return;
        feedback.reset();
        pageTable.reset();
        pool.reset();
        residency.clear();
        requestedPages.clear();
        scheduledBakes.clear();
        initialized = false;
    }

    uint32_t TerrainRVTManager::globalEntryIndex(const VTPageKey& page) const
    {
        return image.pageTableBase
             + vtPageLinearIndex(image.pagesX0, image.pagesY0, page.mip, page.x, page.y);
    }

    glm::vec4 TerrainRVTManager::pageWorldRect(const VTPageKey& page) const
    {
        const uint32_t px = vtPagesAtMip(image.pagesX0, page.mip);
        const uint32_t py = vtPagesAtMip(image.pagesY0, page.mip);
        const float sx = worldExtent.x / static_cast<float>(px);
        const float sz = worldExtent.y / static_cast<float>(py);
        return glm::vec4(worldMin.x + static_cast<float>(page.x) * sx,
                         worldMin.y + static_cast<float>(page.y) * sz,
                         sx, sz);
    }

    void TerrainRVTManager::mapEntry(const VTPageKey& page, uint32_t tile)
    {
        const uint32_t per = pool->getTilesPerSide();
        pageTable->mapEntry(globalEntryIndex(page), tile % per, tile / per);
    }

    void TerrainRVTManager::unmapEntry(const VTPageKey& page)
    {
        pageTable->unmapEntry(globalEntryIndex(page));
    }

    void TerrainRVTManager::schedule(const VTPageKey& page, uint32_t tile)
    {
        scheduledBakes.push_back({page, tile, pageWorldRect(page)});
    }

    void TerrainRVTManager::ensureCoarseResident(uint32_t frame)
    {
        // The single coarsest-mip page is pinned so a lookup always resolves (AC6 fallback).
        VTPageKey coarse{imageId, image.mipCount - 1u, 0u, 0u};
        if (residency.isResident(coarse))
            return;
        const uint32_t tile = pool->allocateTile();
        if (tile == VT_INVALID_TILE)
            return; // pool exhausted at init is not expected, but never crash
        mapEntry(coarse, tile);
        residency.commitAllocation(coarse, tile, frame, /*pinned*/ true);
        schedule(coarse, tile);
    }

    void TerrainRVTManager::beginFrameReadback()
    {
        requestedPages.clear();
        cpuStats.readbackUs = 0;
        cpuStats.decodeUs = 0;
        if (!feedback)
            return;

        const auto readStart = std::chrono::high_resolution_clock::now();
        const std::vector<uint32_t>& words = feedback->readback(); // reused buffer (finding #14)
        const auto readEnd = std::chrono::high_resolution_clock::now();
        cpuStats.readbackUs =
            static_cast<uint64_t>(std::chrono::duration<double, std::micro>(readEnd - readStart).count());

        const auto decodeStart = std::chrono::high_resolution_clock::now();
        if (!words.empty())
        {
            vtForEachSetEntry(words.data(), static_cast<uint32_t>(words.size()), feedback->getTotalEntries(),
                              [&](uint32_t entry)
                              {
                                  uint32_t mip = 0, x = 0, y = 0;
                                  if (vtDecodeEntry(image.pagesX0, image.pagesY0, image.mipCount,
                                                    entry - image.pageTableBase, mip, x, y))
                                      requestedPages.push_back(VTPageKey{imageId, mip, x, y});
                              });
        }

        // Finer mips first: under the per-frame budget the highest-detail pages win.
        std::sort(requestedPages.begin(), requestedPages.end(),
                  [](const VTPageKey& a, const VTPageKey& b) { return a.mip < b.mip; });
        const auto decodeEnd = std::chrono::high_resolution_clock::now();
        cpuStats.decodeUs =
            static_cast<uint64_t>(std::chrono::duration<double, std::micro>(decodeEnd - decodeStart).count());
    }

    void TerrainRVTManager::updateResidency(uint32_t frame, const CoveragePredicate& covered)
    {
        scheduledBakes.clear();
        cpuStats.residencyUs = 0;
        // VK-1610: reset the residency accounting every frame, INCLUDING on the early-out below, so
        // an uninitialized manager reports zeroes rather than the last live frame's numbers.
        cpuStats.requestedPages = static_cast<uint32_t>(requestedPages.size());
        cpuStats.allocatedPages = 0;
        cpuStats.evictedPages = 0;
        cpuStats.uncoveredSkipped = 0;
        cpuStats.unmetPages = 0;
        cpuStats.budgetLimited = false;
        cpuStats.poolLimited = false;
        if (!initialized)
        {
            cpuStats.requestedPages = 0;
            return;
        }

        const auto resStart = std::chrono::high_resolution_clock::now();

        ensureCoarseResident(frame);

        // Finding #8: a pending material change also re-bakes the pinned coarse page in place (it's never
        // evicted/re-requested, so it would otherwise keep stale content at distance). Scheduled here —
        // after this frame's scheduledBakes.clear() above — so recordBakes picks it up.
        if (coarseRebakePending)
        {
            const VTPageKey coarse{imageId, image.mipCount - 1u, 0u, 0u};
            const uint32_t tile = residency.tileFor(coarse);
            if (tile != VT_INVALID_TILE)
                schedule(coarse, tile);
            coarseRebakePending = false;
        }

        const VTResidencyPlan plan = residency.planFrame(
            requestedPages, frame, pool->freeTileCount(),
            config.pagesPerFrame, config.evictionAgeFrames);

        // VK-1610. planFrame clamps toAllocate TWICE - first to config.pagesPerFrame, then to the
        // tiles it can actually free up - and the clamped result alone cannot say which bound bit.
        // Comparing against the unclamped miss count can: falling short of the per-frame cap means
        // the pool ran out of room, which is the real thrash signal.
        const uint32_t planned = static_cast<uint32_t>(plan.toAllocate.size());
        const uint32_t idealAlloc = std::min(config.pagesPerFrame, plan.missCount);
        cpuStats.unmetPages = plan.missCount - planned;
        cpuStats.poolLimited = planned < idealAlloc;
        cpuStats.budgetLimited = !cpuStats.poolLimited && plan.missCount > planned;

        // Evict first so freed tiles are available to the allocations planFrame accounted for.
        for (const auto& e : plan.toEvict)
        {
            const uint32_t tile = residency.commitEviction(e);
            if (tile != VT_INVALID_TILE)
            {
                pool->freeTile(tile);
                unmapEntry(e);
                ++cpuStats.evictedPages;
            }
        }
        for (size_t ai = 0; ai < plan.toAllocate.size(); ++ai)
        {
            const auto& a = plan.toAllocate[ai];
            // Skip pages with no loaded-terrain coverage: baking them yields a fully "uncovered"
            // (alpha 0) tile the shader ignores anyway, so leave them non-resident and spend the
            // budget on pages that carry real detail. (The pinned coarse page above is exempt.)
            if (covered && !covered(pageWorldRect(a)))
            {
                ++cpuStats.uncoveredSkipped;
                continue;
            }
            const uint32_t tile = pool->allocateTile();
            if (tile == VT_INVALID_TILE)
            {
                // Safety break only - NOT the thrash signal. planFrame already clamped toAllocate to
                // freeTiles + evictCount, so reaching this means an eviction it counted on did not
                // free its tile. cpuStats.poolLimited (derived from plan.missCount above) is what
                // actually reports a pool too small for the camera's footprint.
                ++cpuStats.unmetPages;
                break;
            }
            mapEntry(a, tile);
            residency.commitAllocation(a, tile, frame, /*pinned*/ false);
            schedule(a, tile);
            ++cpuStats.allocatedPages;
        }

        const auto resEnd = std::chrono::high_resolution_clock::now();
        cpuStats.residencyUs =
            static_cast<uint64_t>(std::chrono::duration<double, std::micro>(resEnd - resStart).count());
    }

    void TerrainRVTManager::recordBakes(vk::CommandBuffer cmd, const BakeFn& bake)
    {
        if (!initialized || scheduledBakes.empty() || !bake)
            return;

        // Pool: shader-read -> color-attachment for the MRT bake, then back for sampling.
        pool->transition(cmd,
                         vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal,
                         vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                         vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eColorAttachmentWrite);

        bake(cmd, scheduledBakes);

        pool->transition(cmd,
                         vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                         vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader,
                         vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eShaderRead);

        scheduledBakes.clear();
    }

    void TerrainRVTManager::invalidateWorldRect(const glm::vec2& mn, const glm::vec2& mx)
    {
        if (!initialized)
            return;

        for (uint32_t mip = 0; mip + 1u < image.mipCount; ++mip) // skip pinned coarsest mip
        {
            const uint32_t px = vtPagesAtMip(image.pagesX0, mip);
            const uint32_t py = vtPagesAtMip(image.pagesY0, mip);
            const float sx = worldExtent.x / static_cast<float>(px);
            const float sz = worldExtent.y / static_cast<float>(py);

            auto pageOf = [](float rel, float size) -> int32_t
            {
                if (size <= 0.0f) return 0;
                return static_cast<int32_t>(std::floor(rel / size));
            };
            int32_t x0 = pageOf(mn.x - worldMin.x, sx);
            int32_t x1 = pageOf(mx.x - worldMin.x, sx);
            int32_t y0 = pageOf(mn.y - worldMin.y, sz);
            int32_t y1 = pageOf(mx.y - worldMin.y, sz);
            x0 = std::clamp(x0, 0, static_cast<int32_t>(px) - 1);
            x1 = std::clamp(x1, 0, static_cast<int32_t>(px) - 1);
            y0 = std::clamp(y0, 0, static_cast<int32_t>(py) - 1);
            y1 = std::clamp(y1, 0, static_cast<int32_t>(py) - 1);

            for (int32_t y = y0; y <= y1; ++y)
                for (int32_t x = x0; x <= x1; ++x)
                {
                    VTPageKey key{imageId, mip, static_cast<uint32_t>(x), static_cast<uint32_t>(y)};
                    if (!residency.isResident(key))
                        continue;
                    const uint32_t tile = residency.commitEviction(key);
                    if (tile != VT_INVALID_TILE)
                    {
                        pool->freeTile(tile);
                        unmapEntry(key);
                    }
                }
        }

        // VK-1613. updateResidency built this frame's bake list EARLIER in the frame (it runs from the
        // light-occlusion readback, well before updateTerrain calls us), and recordBakes consumes it
        // LATER during command recording — so anything we just evicted above is still sitting in
        // scheduledBakes pointing at a pool tile that no longer belongs to it. Baking into a freed
        // tile corrupts whatever page is handed that tile next.
        //
        // Filtering on residency rather than on the world rect is exact by construction: the loop
        // above is what dropped these pages, so "no longer resident" is precisely "its tile was
        // freed". Evicted pages get re-requested by feedback and re-baked on a later frame.
        //
        // Pre-existing, but VK-1613 turns this from a rare material-change event into something that
        // fires on every layer-visibility toggle, which is what makes it worth closing here.
        std::erase_if(scheduledBakes,
                      [this](const ScheduledBake& b) { return !residency.isResident(b.page); });

        // The pinned coarsest mip is skipped by the loop above but must also refresh after a material
        // change (finding #8) — flag it for an in-place re-bake in the next updateResidency.
        coarseRebakePending = true;
    }

    void TerrainRVTManager::setResidencyBudget(uint32_t pagesPerFrame, uint32_t evictionAgeFrames)
    {
        // The terrain RVT has no upload staging ring, so both apply live (unlike SVT's pagesPerFrame).
        config.pagesPerFrame = std::max(1u, pagesPerFrame);
        config.evictionAgeFrames = std::max(1u, evictionAgeFrames);
    }

    GPUVTImageInfo TerrainRVTManager::getImageInfo() const
    {
        GPUVTImageInfo info;
        info.pagesX0 = image.pagesX0;
        info.pagesY0 = image.pagesY0;
        info.mipCount = image.mipCount;
        info.pageTableBase = image.pageTableBase;
        info.poolDim = pool ? pool->getPoolDim() : 0u;
        return info;
    }
}
