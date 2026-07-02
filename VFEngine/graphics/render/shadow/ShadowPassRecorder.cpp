#include "ShadowPassRecorder.hpp"
#include "VSMPhysicalTilePool.hpp"
#include "ShadowPassPipeline.hpp"
#include "TerrainShadowPipeline.hpp"
#include "../gpudriven/GPUDrivenTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include <chrono>

namespace render::shadow
{
    ShadowPassRecorder::ShadowPassRecorder(core::Device& device)
        : device(device)
    {
    }

    bool ShadowPassRecorder::validatePrerequisites(
        const ShadowPassContext& ctx, ShadowPassPrerequisites& out) const
    {
        if (!ctx.shadowsEnabled || !ctx.shadowPassPipeline ||
            !ctx.shadowPassPipeline->isInitialized() || !ctx.tilePool)
            return false;

        out.hasTerrainShadows = ctx.terrainParams != nullptr &&
                                ctx.terrainParams->tileCount > 0 &&
                                ctx.terrainShadowPipeline != nullptr &&
                                ctx.terrainShadowPipeline->isInitialized();

        out.hasPageViews = !ctx.pageRenderList.empty() ||
                           !ctx.staticPageRenderList.empty() ||
                           !ctx.dynamicPageRenderList.empty();

        if (!out.hasPageViews)
            return false;

        out.hasMeshBatches = ctx.params.batchCount > 0 &&
                             ctx.params.commandsPerSection > 0 &&
                             ctx.params.drawCommandBuffer &&
                             ctx.params.drawCountBuffer;

        if (!out.hasMeshBatches && !out.hasTerrainShadows)
            return false;

        return true;
    }

    void ShadowPassRecorder::beginDynamicShadowPass(
        vk::CommandBuffer cmd, VSMPhysicalTilePool* tilePool, bool clearDepth)
    {
        core::DynamicRenderingInfo info{};
        info.extent = vk::Extent2D{vsm::PHYSICAL_POOL_DIM, vsm::PHYSICAL_POOL_DIM};

        if (clearDepth)
            info.depthAttachment = core::depthClear(tilePool->getPoolImageView(), 1.0f, 0);
        else
            info.depthAttachment = core::depthLoad(tilePool->getPoolImageView());

        core::beginDynamicRendering(cmd, info);
    }

    void ShadowPassRecorder::endDynamicShadowPass(vk::CommandBuffer cmd)
    {
        core::endDynamicRendering(cmd);
    }

    void ShadowPassRecorder::bindShadowPipelineAndSets(
        vk::CommandBuffer cmd, const ShadowPassContext& ctx)
    {
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                         ctx.shadowPassPipeline->getPipeline());

        if (ctx.params.shadowCullEnabled)
        {
            // B1: set 0 (per-draw data) is chosen per page in recordTileCommands (bin PerDrawData
            // for bin pages, main PerDrawData for legacy-fallback pages), so bind only sets 1-4 here.
            std::array<vk::DescriptorSet, 4> descriptorSets = {
                ctx.params.meshletDataDescSet, ctx.params.vertexDataDescSet,
                ctx.params.boneMatrixDescSet, ctx.params.cameraDescSet
            };
            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                ctx.shadowPassPipeline->getPipelineLayout(),
                1, static_cast<uint32_t>(descriptorSets.size()),
                descriptorSets.data(), 0, nullptr);
            return;
        }

        std::array<vk::DescriptorSet, 5> descriptorSets = {
            ctx.params.perDrawDataDescSet, ctx.params.meshletDataDescSet,
            ctx.params.vertexDataDescSet, ctx.params.boneMatrixDescSet,
            ctx.params.cameraDescSet
        };
        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            ctx.shadowPassPipeline->getPipelineLayout(),
            0, static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(), 0, nullptr);
    }

    void ShadowPassRecorder::dispatchBinnedPage(
        vk::CommandBuffer cmd, const ShadowPassContext& ctx, const PageRenderEntry& page)
    {
        ShadowPushConstants pc = buildTilePushConstants(page);
        pc.baseDrawIndex = page.binSlot * ctx.params.binCapacity;
        // B1 bins are single-layer (all casters), so no static/dynamic filter (B3 splits them).
        pc.objectFilterMask = 0;
        pc.objectFilterValue = 0;

        cmd.pushConstants(
            ctx.shadowPassPipeline->getPipelineLayout(),
            vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
            0, sizeof(ShadowPushConstants), &pc);

        const vk::DeviceSize cmdOffset =
            static_cast<vk::DeviceSize>(page.binSlot) * ctx.params.binCapacity *
            sizeof(vk::DrawMeshTasksIndirectCommandEXT);
        const vk::DeviceSize cntOffset =
            static_cast<vk::DeviceSize>(page.binSlot) * sizeof(uint32_t);

        render::FrameDrawStats::count(render::DrawCategory::Shadows);
        cmd.drawMeshTasksIndirectCountEXT(
            ctx.params.binCommandBuffer, cmdOffset,
            ctx.params.binCountBuffer, cntOffset,
            ctx.params.binCapacity,
            sizeof(vk::DrawMeshTasksIndirectCommandEXT));
    }

    void ShadowPassRecorder::executeTileCopies(
        vk::CommandBuffer cmd, VSMPhysicalTilePool* tilePool,
        const std::vector<TileCopyEntry>& copies)
    {
        VSMPhysicalTilePool::transitionPoolToTransfer(cmd, tilePool);

        std::vector<vk::ImageCopy> copyRegions;
        copyRegions.reserve(copies.size());
        for (const auto& entry : copies)
        {
            copyRegions.push_back(
                tilePool->getTileCopyRegion(entry.srcTileIndex, entry.dstTileIndex));
        }

        cmd.copyImage(
            tilePool->getPoolImage(), vk::ImageLayout::eGeneral,
            tilePool->getPoolImage(), vk::ImageLayout::eGeneral,
            static_cast<uint32_t>(copyRegions.size()), copyRegions.data());

        VSMPhysicalTilePool::transitionPoolFromTransfer(cmd, tilePool);
    }

    void ShadowPassRecorder::transitionPoolToDepthAttachment(
        vk::CommandBuffer cmd, VSMPhysicalTilePool* tilePool, bool poolFirstUse)
    {
        vk::ImageMemoryBarrier barrier{};
        barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        barrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = tilePool->getPoolImage();
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vk::PipelineStageFlags srcStage;
        if (poolFirstUse)
        {
            barrier.srcAccessMask = {};
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
        }
        else
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
            barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            srcStage = vk::PipelineStageFlagBits::eFragmentShader;
        }

        cmd.pipelineBarrier(srcStage, vk::PipelineStageFlagBits::eEarlyFragmentTests,
                            {}, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void ShadowPassRecorder::transitionPoolToShaderRead(
        vk::CommandBuffer cmd, VSMPhysicalTilePool* tilePool)
    {
        vk::ImageMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = tilePool->getPoolImage();
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eLateFragmentTests,
                            vk::PipelineStageFlagBits::eFragmentShader,
                            {}, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void ShadowPassRecorder::dispatchLegacyMeshBatches(
        vk::CommandBuffer cmd, const ShadowPassContext& ctx, ShadowPushConstants& pc)
    {
        for (uint32_t sg = 0; sg < ctx.params.shaderGroupCount; ++sg)
        {
            if (sg == ctx.params.transparentGroupIndex) continue;

            for (uint32_t batch = 0; batch < ctx.params.batchCount; ++batch)
            {
                uint32_t section = batch * ctx.params.shaderGroupCount + sg;
                // A4: skip sections with no candidates (matches the main draw / depth-prepass loops).
                // Occupancy is a camera-independent superset, so an empty section casts nothing.
                if (ctx.params.sectionOccupancy &&
                    section < ctx.params.sectionOccupancy->size() &&
                    (*ctx.params.sectionOccupancy)[section] == 0)
                    continue;
                pc.baseDrawIndex = section * ctx.params.commandsPerSection;

                cmd.pushConstants(
                    ctx.shadowPassPipeline->getPipelineLayout(),
                    vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
                    0, sizeof(ShadowPushConstants), &pc);

                vk::DeviceSize cmdOffset = section * ctx.params.commandsPerSection *
                    sizeof(vk::DrawMeshTasksIndirectCommandEXT);
                vk::DeviceSize cntOffset = section * ctx.params.drawCountStructSize;

                render::FrameDrawStats::count(render::DrawCategory::Shadows);
                cmd.drawMeshTasksIndirectCountEXT(
                    ctx.params.drawCommandBuffer, cmdOffset,
                    ctx.params.drawCountBuffer, cntOffset,
                    ctx.params.commandsPerSection,
                    sizeof(vk::DrawMeshTasksIndirectCommandEXT));
            }
        }
    }

    void ShadowPassRecorder::dispatchTerrainShadow(
        vk::CommandBuffer cmd, const ShadowPassContext& ctx,
        const glm::mat4& viewProj, float depthBias, float slopeBias)
    {
        if (ctx.terrainParams == nullptr || ctx.terrainParams->tileCount == 0 ||
            ctx.terrainShadowPipeline == nullptr || !ctx.terrainShadowPipeline->isInitialized())
            return;

        constexpr float terrainBiasScale = 3.5f;
        ctx.terrainShadowPipeline->dispatch(
            cmd,
            ctx.terrainParams->terrainDataDescSet,
            ctx.terrainParams->terrainMeshletDescSet,
            ctx.terrainParams->terrainVertexDescSet,
            viewProj,
            ctx.terrainParams->tileCount,
            depthBias * terrainBiasScale,
            slopeBias * terrainBiasScale);
    }

    void ShadowPassRecorder::clearTileDepth(
        vk::CommandBuffer cmd, const vk::Rect2D& scissor)
    {
        vk::ClearAttachment clearAttach{};
        clearAttach.aspectMask = vk::ImageAspectFlagBits::eDepth;
        clearAttach.clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

        vk::ClearRect clearRect{};
        clearRect.rect = scissor;
        clearRect.baseArrayLayer = 0;
        clearRect.layerCount = 1;

        cmd.clearAttachments(1, &clearAttach, 1, &clearRect);
    }

    ShadowPushConstants ShadowPassRecorder::buildTilePushConstants(
        const PageRenderEntry& page)
    {
        ShadowPushConstants pc{};
        pc.lightViewProjection = page.cropViewProjection;
        pc.baseDrawIndex = 0;
        pc.depthBias = page.depthBias;
        pc.slopeBias = page.slopeBias;
        pc.normalBias = page.normalBias;

        if (page.layer == ShadowLayer::Static)
        {
            pc.objectFilterMask = gpudriven::ObjectFlags::ShadowStatic;
            pc.objectFilterValue = gpudriven::ObjectFlags::ShadowStatic;
        }
        else if (page.layer == ShadowLayer::Dynamic)
        {
            pc.objectFilterMask = gpudriven::ObjectFlags::ShadowStatic;
            pc.objectFilterValue = 0;
        }
        return pc;
    }

    void ShadowPassRecorder::recordTileCommands(
        vk::CommandBuffer cmd, const PageRenderEntry& page,
        const ShadowPassContext& ctx, bool clearTile)
    {
        vk::Viewport viewport = ctx.tilePool->getTileViewport(page.physicalTileIndex);
        cmd.setViewport(0, 1, &viewport);

        vk::Rect2D scissor = ctx.tilePool->getTileScissor(page.physicalTileIndex);
        cmd.setScissor(0, 1, &scissor);

        if (clearTile)
            clearTileDepth(cmd, scissor);

        cmd.setDepthBias(page.depthBias, 0.0f, page.slopeBias);

        const bool binPath = ctx.params.shadowCullEnabled &&
                             page.binSlot != INVALID_BIN_SLOT &&
                             ctx.params.binCommandBuffer && ctx.params.binPerDrawDataDescSet;

        // B1: when the flag is on, bindShadowPipelineAndSets bound only sets 1-4, so bind set 0 per
        // page here — the bin PerDrawData set for bin pages, the main set for legacy-fallback pages.
        if (ctx.params.shadowCullEnabled)
        {
            vk::DescriptorSet set0 = binPath ? ctx.params.binPerDrawDataDescSet
                                             : ctx.params.perDrawDataDescSet;
            if (set0)
                cmd.bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics,
                    ctx.shadowPassPipeline->getPipelineLayout(),
                    0, 1, &set0, 0, nullptr);
        }

        if (binPath)
        {
            dispatchBinnedPage(cmd, ctx, page);
        }
        else
        {
            const bool legacyReady = ctx.params.batchCount > 0 && ctx.params.commandsPerSection > 0 &&
                                     ctx.params.drawCommandBuffer && ctx.params.drawCountBuffer;
            if (legacyReady)
            {
                ShadowPushConstants pc = buildTilePushConstants(page);
                dispatchLegacyMeshBatches(cmd, ctx, pc);
            }
        }

        dispatchTerrainShadow(cmd, ctx, page.cropViewProjection,
                              page.depthBias, page.slopeBias);
    }

    void ShadowPassRecorder::recordStaticPhase(
        vk::CommandBuffer cmd, const ShadowPassContext& ctx,
        const ShadowPassPrerequisites& prereq, bool clearDepth)
    {
        bool hasLegacyOrStaticPages = !ctx.pageRenderList.empty() ||
                                      !ctx.staticPageRenderList.empty();
        if (!hasLegacyOrStaticPages)
            return;

        beginDynamicShadowPass(cmd, ctx.tilePool, clearDepth);

        if (prereq.hasMeshBatches)
            bindShadowPipelineAndSets(cmd, ctx);

        bool loadPass = !clearDepth;
        for (const auto& page : ctx.pageRenderList)
            recordTileCommands(cmd, page, ctx, loadPass);

        for (const auto& page : ctx.staticPageRenderList)
            recordTileCommands(cmd, page, ctx, loadPass);

        endDynamicShadowPass(cmd);
    }

    void ShadowPassRecorder::recordDynamicPhase(
        vk::CommandBuffer cmd, const ShadowPassContext& ctx,
        const ShadowPassPrerequisites& prereq)
    {
        if (ctx.dynamicPageRenderList.empty())
            return;

        beginDynamicShadowPass(cmd, ctx.tilePool, false);

        if (prereq.hasMeshBatches)
            bindShadowPipelineAndSets(cmd, ctx);

        for (const auto& page : ctx.dynamicPageRenderList)
            recordTileCommands(cmd, page, ctx, true);

        endDynamicShadowPass(cmd);
    }

    void ShadowPassRecorder::recordShadowPass(
        vk::CommandBuffer cmd, const ShadowPassContext& ctx)
    {
        ShadowPassPrerequisites prereq;
        if (!validatePrerequisites(ctx, prereq))
        {
            lastStats = {};
            return;
        }

        auto recordStart = std::chrono::high_resolution_clock::now();

        if (prereq.hasPageViews)
        {
            transitionPoolToDepthAttachment(cmd, ctx.tilePool, ctx.poolFirstUse);
            bool clearDepth = ctx.poolFirstUse;

            recordStaticPhase(cmd, ctx, prereq, clearDepth);

            if (!ctx.tileCopyList.empty())
                executeTileCopies(cmd, ctx.tilePool, ctx.tileCopyList);

            recordDynamicPhase(cmd, ctx, prereq);

            transitionPoolToShaderRead(cmd, ctx.tilePool);
        }

        auto recordEnd = std::chrono::high_resolution_clock::now();
        lastStats.recordingUs = std::chrono::duration<float, std::micro>(
            recordEnd - recordStart).count();
        lastStats.tileCount = static_cast<uint32_t>(
            ctx.pageRenderList.size() + ctx.staticPageRenderList.size() +
            ctx.dynamicPageRenderList.size());
        lastStats.threadsUsed = 0;
        lastStats.usedParallel = false;

    }
}
