#include "ShadowPassRecorder.hpp"
#include "VSMPhysicalTilePool.hpp"
#include "ShadowPassPipeline.hpp"
#include "TerrainShadowPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/ThreadCommandPoolManager.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "threading/JobSystem.hpp"
#include <chrono>

namespace render::shadow
{
    void ShadowPassRecorder::recordSecondaryTileCommands(
        const ParallelDispatchArgs& args,
        const std::vector<PageRenderEntry>& pages,
        bool clearTiles,
        uint32_t slot,
        std::vector<vk::CommandBuffer>& secondaryBuffers,
        std::vector<bool>& threadUsed)
    {
        const auto& ctx = *args.ctx;
        uint32_t tileCount = static_cast<uint32_t>(pages.size());
        if (tileCount == 0)
            return;

        vk::Format poolDepthFormat = ctx.tilePool->getDepthFormat();

        // Partition the pages into a fixed set of contiguous chunks and key each chunk's
        // secondary buffer by CHUNK INDEX, not by enkiTS threadNum. JobSystem::parallelFor
        // (enkiTS TaskSet with m_MinRange=1) can split the work into more partitions than
        // worker threads, so a single thread may run several partitions; keying the buffer by
        // threadNum would re-record (begin/end) the same secondary and silently drop the
        // earlier partition's tiles. Each chunk owns threadPools[chunk]'s buffer instead, so it
        // is recorded exactly once. chunkCount <= threadCount keeps getSecondary() in bounds,
        // and each pool has a single consumer (no concurrent recording of one pool).
        uint32_t chunkCount = std::min(args.threadPoolManager->getThreadCount(), tileCount);
        uint32_t chunkSize = (tileCount + chunkCount - 1) / chunkCount;

        threading::JobSystem::instance().parallelFor(chunkCount,
            [&](uint32_t begin, uint32_t end, uint32_t /*threadNum*/) {
                for (uint32_t c = begin; c < end; ++c)
                {
                    uint32_t tileBegin = c * chunkSize;
                    uint32_t tileEnd = std::min(tileBegin + chunkSize, tileCount);
                    if (tileBegin >= tileEnd)
                        continue;

                    // Distinct slot per phase: the static and dynamic phases both execute their
                    // secondaries into the same primary, so they must NOT share buffers (re-
                    // recording an already-executed secondary invalidates the primary).
                    vk::CommandBuffer secondary =
                        args.threadPoolManager->getSecondary(c, args.frameIndex, slot);

                    // Dynamic rendering inheritance for secondary command buffers
                    vk::CommandBufferInheritanceRenderingInfo inheritanceRendering{};
                    inheritanceRendering.depthAttachmentFormat = poolDepthFormat;
                    inheritanceRendering.rasterizationSamples = vk::SampleCountFlagBits::e1;

                    vk::CommandBufferInheritanceInfo inheritance{};
                    inheritance.pNext = &inheritanceRendering;

                    vk::CommandBufferBeginInfo beginInfo{};
                    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit |
                                     vk::CommandBufferUsageFlagBits::eRenderPassContinue;
                    beginInfo.pInheritanceInfo = &inheritance;
                    secondary.begin(beginInfo);

                    for (uint32_t i = tileBegin; i < tileEnd; ++i)
                        recordTileCommands(secondary, pages[i], ctx, clearTiles);

                    secondary.end();
                    secondaryBuffers[c] = secondary;
                    threadUsed[c] = true;
                }
            }, 1);
    }

    void ShadowPassRecorder::dispatchPagesParallel(
        const ParallelDispatchArgs& args,
        const std::vector<PageRenderEntry>& pages,
        bool clearTiles,
        uint32_t slot)
    {
        uint32_t threadCount = args.threadPoolManager->getThreadCount();
        std::vector<vk::CommandBuffer> secondaryBuffers(threadCount, nullptr);
        std::vector<bool> threadUsed(threadCount, false);

        recordSecondaryTileCommands(args, pages, clearTiles, slot, secondaryBuffers, threadUsed);

        std::vector<vk::CommandBuffer> validSecondaries;
        for (uint32_t t = 0; t < threadCount; t++)
            if (threadUsed[t]) validSecondaries.push_back(secondaryBuffers[t]);

        if (!validSecondaries.empty())
            args.primaryCmd.executeCommands(
                static_cast<uint32_t>(validSecondaries.size()),
                validSecondaries.data());
    }

    void ShadowPassRecorder::recordStaticPhaseParallel(
        const ParallelDispatchArgs& args, bool clearDepth)
    {
        const auto& ctx = *args.ctx;
        bool hasLegacyOrStatic = !ctx.pageRenderList.empty() ||
                                 !ctx.staticPageRenderList.empty();
        if (!hasLegacyOrStatic)
            return;

        std::vector<PageRenderEntry> combinedPhaseA;
        combinedPhaseA.reserve(
            ctx.pageRenderList.size() + ctx.staticPageRenderList.size());
        combinedPhaseA.insert(combinedPhaseA.end(),
            ctx.pageRenderList.begin(), ctx.pageRenderList.end());
        combinedPhaseA.insert(combinedPhaseA.end(),
            ctx.staticPageRenderList.begin(), ctx.staticPageRenderList.end());

        // Begin dynamic rendering with secondary command buffer execution
        core::DynamicRenderingInfo info{};
        info.extent = vk::Extent2D{vsm::PHYSICAL_POOL_DIM, vsm::PHYSICAL_POOL_DIM};

        if (clearDepth)
            info.depthAttachment = core::depthClear(ctx.tilePool->getPoolImageView(), 1.0f, 0);
        else
            info.depthAttachment = core::depthLoad(ctx.tilePool->getPoolImageView());

        // Use eContentsSecondaryCommandBuffers for parallel recording
        vk::RenderingInfo renderingInfo{};
        renderingInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderingInfo.renderArea.extent = info.extent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 0;
        renderingInfo.pColorAttachments = nullptr;
        renderingInfo.pDepthAttachment = &info.depthAttachment.value();
        renderingInfo.flags = vk::RenderingFlagBits::eContentsSecondaryCommandBuffers;

        args.primaryCmd.beginRendering(renderingInfo);
        bool loadPass = !clearDepth;
        dispatchPagesParallel(args, combinedPhaseA, loadPass, /*slot=*/0);
        args.primaryCmd.endRendering();
    }

    void ShadowPassRecorder::recordDynamicPhaseParallel(
        const ParallelDispatchArgs& args)
    {
        const auto& ctx = *args.ctx;
        if (ctx.dynamicPageRenderList.empty())
            return;

        core::DynamicRenderingInfo info{};
        info.extent = vk::Extent2D{vsm::PHYSICAL_POOL_DIM, vsm::PHYSICAL_POOL_DIM};
        info.depthAttachment = core::depthLoad(ctx.tilePool->getPoolImageView());

        // Use eContentsSecondaryCommandBuffers for parallel recording
        vk::RenderingInfo renderingInfo{};
        renderingInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderingInfo.renderArea.extent = info.extent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 0;
        renderingInfo.pColorAttachments = nullptr;
        renderingInfo.pDepthAttachment = &info.depthAttachment.value();
        renderingInfo.flags = vk::RenderingFlagBits::eContentsSecondaryCommandBuffers;

        args.primaryCmd.beginRendering(renderingInfo);
        dispatchPagesParallel(args, ctx.dynamicPageRenderList, true, /*slot=*/1);
        args.primaryCmd.endRendering();
    }

    void ShadowPassRecorder::recordShadowPassParallel(
        vk::CommandBuffer primaryCmd,
        const ShadowPassContext& ctx,
        core::ThreadCommandPoolManager* threadPoolManager,
        uint32_t frameIndex)
    {
        ShadowPassPrerequisites prereq;
        if (!validatePrerequisites(ctx, prereq))
        {
            lastStats = {};
            return;
        }

        ParallelDispatchArgs args{primaryCmd, &ctx, threadPoolManager, frameIndex};

        if (prereq.hasPageViews)
        {
            transitionPoolToDepthAttachment(primaryCmd, ctx.tilePool, ctx.poolFirstUse);
            bool clearDepth = ctx.poolFirstUse;

            auto recordStart = std::chrono::high_resolution_clock::now();

            recordStaticPhaseParallel(args, clearDepth);

            if (!ctx.tileCopyList.empty())
                executeTileCopies(primaryCmd, ctx.tilePool, ctx.tileCopyList);

            recordDynamicPhaseParallel(args);

            auto recordEnd = std::chrono::high_resolution_clock::now();
            lastStats.recordingUs = std::chrono::duration<float, std::micro>(
                recordEnd - recordStart).count();
            lastStats.tileCount = static_cast<uint32_t>(
                ctx.pageRenderList.size() + ctx.staticPageRenderList.size() +
                ctx.dynamicPageRenderList.size());
            uint32_t totalTiles = lastStats.tileCount;
            lastStats.threadsUsed = std::min(totalTiles, threadPoolManager->getThreadCount());
            lastStats.usedParallel = true;

            transitionPoolToShaderRead(primaryCmd, ctx.tilePool);
        }
    }
}
