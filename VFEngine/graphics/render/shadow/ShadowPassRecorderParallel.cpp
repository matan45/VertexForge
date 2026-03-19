#include "ShadowPassRecorder.hpp"
#include "VSMPhysicalTilePool.hpp"
#include "ShadowResourcePool.hpp"
#include "ShadowPassPipeline.hpp"
#include "TerrainShadowPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/ThreadCommandPoolManager.hpp"
#include "threading/JobSystem.hpp"
#include <chrono>

namespace render::shadow
{
    void ShadowPassRecorder::dispatchPagesParallel(
        const ParallelDispatchArgs& args,
        const std::vector<PageRenderEntry>& pages,
        vk::RenderPass renderPass,
        vk::Framebuffer framebuffer,
        bool useLoadPass)
    {
        uint32_t tileCount = static_cast<uint32_t>(pages.size());
        uint32_t threadCount = args.threadPoolManager->getThreadCount();

        std::vector<vk::CommandBuffer> secondaryBuffers(threadCount, nullptr);
        std::vector<bool> threadUsed(threadCount, false);

        const auto& ctx = *args.ctx;
        const auto& prereq = *args.prereq;

        threading::JobSystem::instance().parallelFor(tileCount,
            [&](uint32_t begin, uint32_t end, uint32_t threadNum) {
                vk::CommandBuffer secondary =
                    args.threadPoolManager->getSecondary(threadNum, args.frameIndex);

                vk::CommandBufferInheritanceInfo inheritance{};
                inheritance.renderPass = renderPass;
                inheritance.subpass = 0;
                inheritance.framebuffer = framebuffer;

                vk::CommandBufferBeginInfo beginInfo{};
                beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit |
                                 vk::CommandBufferUsageFlagBits::eRenderPassContinue;
                beginInfo.pInheritanceInfo = &inheritance;
                secondary.begin(beginInfo);

                if (prereq.hasMeshBatches)
                    bindShadowPipelineAndSets(secondary, ctx);

                for (uint32_t i = begin; i < end; ++i)
                    recordTileCommands(secondary, pages[i], ctx, useLoadPass);

                secondary.end();
                secondaryBuffers[threadNum] = secondary;
                threadUsed[threadNum] = true;
            }, 1);

        std::vector<vk::CommandBuffer> validSecondaries;
        for (uint32_t t = 0; t < threadCount; t++)
            if (threadUsed[t]) validSecondaries.push_back(secondaryBuffers[t]);

        if (!validSecondaries.empty())
            args.primaryCmd.executeCommands(
                static_cast<uint32_t>(validSecondaries.size()),
                validSecondaries.data());
    }

    void ShadowPassRecorder::recordStaticPhaseParallel(
        const ParallelDispatchArgs& args, bool useLoadPass)
    {
        const auto& ctx = *args.ctx;
        bool hasLegacyOrStatic = !ctx.pageRenderList.empty() ||
                                 !ctx.staticPageRenderList.empty();
        if (!hasLegacyOrStatic)
            return;

        vk::ClearValue clearValue{};
        clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

        vk::RenderPass rp;
        vk::Framebuffer fb;
        vk::RenderPassBeginInfo rpInfo{};

        if (useLoadPass)
        {
            rp = ctx.tilePool->getRenderPassLoad();
            fb = ctx.tilePool->getFramebufferLoad();
            rpInfo.clearValueCount = 0;
            rpInfo.pClearValues = nullptr;
        }
        else
        {
            rp = ctx.tilePool->getRenderPass();
            fb = ctx.tilePool->getFramebuffer();
            rpInfo.clearValueCount = 1;
            rpInfo.pClearValues = &clearValue;
        }
        rpInfo.renderPass = rp;
        rpInfo.framebuffer = fb;
        rpInfo.renderArea.offset = vk::Offset2D{0, 0};
        rpInfo.renderArea.extent = vk::Extent2D{
            vsm::PHYSICAL_POOL_DIM, vsm::PHYSICAL_POOL_DIM};

        std::vector<PageRenderEntry> combinedPhaseA;
        combinedPhaseA.reserve(
            ctx.pageRenderList.size() + ctx.staticPageRenderList.size());
        combinedPhaseA.insert(combinedPhaseA.end(),
            ctx.pageRenderList.begin(), ctx.pageRenderList.end());
        combinedPhaseA.insert(combinedPhaseA.end(),
            ctx.staticPageRenderList.begin(), ctx.staticPageRenderList.end());

        args.primaryCmd.beginRenderPass(rpInfo,
            vk::SubpassContents::eSecondaryCommandBuffers);
        dispatchPagesParallel(args, combinedPhaseA, rp, fb, useLoadPass);
        args.primaryCmd.endRenderPass();
    }

    void ShadowPassRecorder::recordDynamicPhaseParallel(
        const ParallelDispatchArgs& args)
    {
        const auto& ctx = *args.ctx;
        if (ctx.dynamicPageRenderList.empty())
            return;

        vk::RenderPass rp = ctx.tilePool->getRenderPassLoad();
        vk::Framebuffer fb = ctx.tilePool->getFramebufferLoad();

        vk::RenderPassBeginInfo rpInfo{};
        rpInfo.renderPass = rp;
        rpInfo.framebuffer = fb;
        rpInfo.clearValueCount = 0;
        rpInfo.pClearValues = nullptr;
        rpInfo.renderArea.offset = vk::Offset2D{0, 0};
        rpInfo.renderArea.extent = vk::Extent2D{
            vsm::PHYSICAL_POOL_DIM, vsm::PHYSICAL_POOL_DIM};

        args.primaryCmd.beginRenderPass(rpInfo,
            vk::SubpassContents::eSecondaryCommandBuffers);
        dispatchPagesParallel(args, ctx.dynamicPageRenderList, rp, fb, true);
        args.primaryCmd.endRenderPass();
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

        ParallelDispatchArgs args{primaryCmd, &ctx, &prereq,
                                  threadPoolManager, frameIndex};

        if (prereq.hasPageViews)
        {
            transitionPoolToDepthAttachment(primaryCmd, ctx.tilePool, ctx.poolFirstUse);
            bool useLoadPass = !ctx.poolFirstUse;

            auto recordStart = std::chrono::high_resolution_clock::now();

            recordStaticPhaseParallel(args, useLoadPass);

            if (!ctx.tileCopyList.empty())
                executeTileCopies(primaryCmd, ctx.tilePool, ctx.tileCopyList);

            recordDynamicPhaseParallel(args);

            auto recordEnd = std::chrono::high_resolution_clock::now();
            lastStats.recordingUs = std::chrono::duration<float, std::micro>(
                recordEnd - recordStart).count();
            lastStats.tileCount = static_cast<uint32_t>(
                ctx.pageRenderList.size() + ctx.staticPageRenderList.size() +
                ctx.dynamicPageRenderList.size());
            lastStats.threadsUsed = threadPoolManager->getThreadCount();
            lastStats.usedParallel = true;

            transitionPoolToShaderRead(primaryCmd, ctx.tilePool);
        }

        renderPointLightCubeShadows(primaryCmd, ctx);
    }
}
