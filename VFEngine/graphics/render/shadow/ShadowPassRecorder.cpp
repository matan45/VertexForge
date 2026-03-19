#include "ShadowPassRecorder.hpp"
#include "VSMPhysicalTilePool.hpp"
#include "ShadowResourcePool.hpp"
#include "ShadowPassPipeline.hpp"
#include "TerrainShadowPipeline.hpp"
#include "../../core/Device.hpp"
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

        out.hasPointShadows = false;
        for (const auto& [entityId, data] : ctx.lightShadowData)
        {
            if (data.type == ShadowMapType::PointCube &&
                data.settings.enabled && data.settings.castShadows &&
                data.resourceHandle.isValid())
            {
                out.hasPointShadows = true;
                break;
            }
        }

        if (!out.hasPageViews && !out.hasPointShadows)
            return false;

        out.hasMeshBatches = ctx.params.batchCount > 0 &&
                             ctx.params.commandsPerSection > 0 &&
                             ctx.params.drawCommandBuffer &&
                             ctx.params.drawCountBuffer;

        if (!out.hasMeshBatches && !out.hasTerrainShadows)
            return false;

        return true;
    }

    void ShadowPassRecorder::beginTileRenderPass(
        vk::CommandBuffer cmd, VSMPhysicalTilePool* tilePool, bool useLoadPass)
    {
        vk::ClearValue clearValue{};
        clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

        vk::RenderPassBeginInfo renderPassInfo{};
        if (useLoadPass)
        {
            renderPassInfo.renderPass = tilePool->getRenderPassLoad();
            renderPassInfo.framebuffer = tilePool->getFramebufferLoad();
            renderPassInfo.clearValueCount = 0;
            renderPassInfo.pClearValues = nullptr;
        }
        else
        {
            renderPassInfo.renderPass = tilePool->getRenderPass();
            renderPassInfo.framebuffer = tilePool->getFramebuffer();
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &clearValue;
        }
        renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderPassInfo.renderArea.extent = vk::Extent2D{
            vsm::PHYSICAL_POOL_DIM, vsm::PHYSICAL_POOL_DIM};

        cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
    }

    void ShadowPassRecorder::bindShadowPipelineAndSets(
        vk::CommandBuffer cmd, const ShadowPassContext& ctx)
    {
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                         ctx.shadowPassPipeline->getPipeline());

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

    void ShadowPassRecorder::dispatchMeshBatches(
        vk::CommandBuffer cmd, const ShadowPassContext& ctx,
        ShadowPushConstants& pc)
    {
        for (uint32_t sg = 0; sg < ctx.params.shaderGroupCount; ++sg)
        {
            if (sg == ctx.params.transparentGroupIndex) continue;

            for (uint32_t batch = 0; batch < ctx.params.batchCount; ++batch)
            {
                uint32_t section = batch * ctx.params.shaderGroupCount + sg;
                pc.baseDrawIndex = section * ctx.params.commandsPerSection;

                cmd.pushConstants(
                    ctx.shadowPassPipeline->getPipelineLayout(),
                    vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
                    0, sizeof(ShadowPushConstants), &pc);

                vk::DeviceSize cmdOffset = section * ctx.params.commandsPerSection *
                    sizeof(vk::DrawMeshTasksIndirectCommandEXT);
                vk::DeviceSize cntOffset = section * ctx.params.drawCountStructSize;

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

        constexpr float terrainBiasScale = 4.0f;
        ctx.terrainShadowPipeline->dispatch(
            cmd,
            ctx.terrainParams->terrainDataDescSet,
            ctx.terrainParams->terrainMeshletDescSet,
            ctx.terrainParams->terrainVertexDescSet,
            viewProj,
            ctx.terrainParams->tileCount,
            ctx.terrainParams->shadowLOD,
            depthBias * terrainBiasScale,
            slopeBias * terrainBiasScale);
    }

    void ShadowPassRecorder::recordTileCommands(
        vk::CommandBuffer cmd, const PageRenderEntry& page,
        const ShadowPassContext& ctx, bool useLoadPass)
    {
        vk::Viewport viewport = ctx.tilePool->getTileViewport(page.physicalTileIndex);
        cmd.setViewport(0, 1, &viewport);

        vk::Rect2D scissor = ctx.tilePool->getTileScissor(page.physicalTileIndex);
        cmd.setScissor(0, 1, &scissor);

        if (useLoadPass)
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

        cmd.setDepthBias(page.depthBias, 0.0f, page.slopeBias);

        if (ctx.params.batchCount > 0 && ctx.params.commandsPerSection > 0 &&
            ctx.params.drawCommandBuffer && ctx.params.drawCountBuffer)
        {
            ShadowPushConstants pc{};
            pc.lightViewProjection = page.cropViewProjection;
            pc.baseDrawIndex = 0;
            pc.depthBias = page.depthBias;
            pc.slopeBias = page.slopeBias;
            pc.normalBias = page.normalBias;

            constexpr uint32_t FLAG_SHADOW_STATIC = 1u << 17u;
            if (page.layer == ShadowLayer::Static)
            {
                pc.objectFilterMask = FLAG_SHADOW_STATIC;
                pc.objectFilterValue = FLAG_SHADOW_STATIC;
            }
            else if (page.layer == ShadowLayer::Dynamic)
            {
                pc.objectFilterMask = FLAG_SHADOW_STATIC;
                pc.objectFilterValue = 0;
            }

            dispatchMeshBatches(cmd, ctx, pc);
        }

        dispatchTerrainShadow(cmd, ctx, page.cropViewProjection,
                              page.depthBias, page.slopeBias);
    }

    void ShadowPassRecorder::recordStaticPhase(
        vk::CommandBuffer cmd, const ShadowPassContext& ctx,
        const ShadowPassPrerequisites& prereq, bool useLoadPass)
    {
        bool hasLegacyOrStaticPages = !ctx.pageRenderList.empty() ||
                                      !ctx.staticPageRenderList.empty();
        if (!hasLegacyOrStaticPages)
            return;

        beginTileRenderPass(cmd, ctx.tilePool, useLoadPass);

        if (prereq.hasMeshBatches)
            bindShadowPipelineAndSets(cmd, ctx);

        for (const auto& page : ctx.pageRenderList)
            recordTileCommands(cmd, page, ctx, useLoadPass);

        for (const auto& page : ctx.staticPageRenderList)
            recordTileCommands(cmd, page, ctx, useLoadPass);

        cmd.endRenderPass();
    }

    void ShadowPassRecorder::recordDynamicPhase(
        vk::CommandBuffer cmd, const ShadowPassContext& ctx,
        const ShadowPassPrerequisites& prereq)
    {
        if (ctx.dynamicPageRenderList.empty())
            return;

        beginTileRenderPass(cmd, ctx.tilePool, true);

        if (prereq.hasMeshBatches)
            bindShadowPipelineAndSets(cmd, ctx);

        for (const auto& page : ctx.dynamicPageRenderList)
            recordTileCommands(cmd, page, ctx, true);

        cmd.endRenderPass();
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
            bool useLoadPass = !ctx.poolFirstUse;

            recordStaticPhase(cmd, ctx, prereq, useLoadPass);

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

        renderPointLightCubeShadows(cmd, ctx);
    }

    void ShadowPassRecorder::renderCubeFace(
        vk::CommandBuffer cmd, const ShadowPassContext& ctx,
        const ShadowPassPrerequisites& prereq,
        const CubeFaceRenderInfo& faceInfo)
    {
        const auto& view = faceInfo.view;
        uint32_t cubeSize = faceInfo.cubeSize;

        vk::ClearValue clearValue{};
        clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.renderPass = ctx.shadowPassPipeline->getRenderPass();
        renderPassInfo.framebuffer = faceInfo.framebuffer;
        renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderPassInfo.renderArea.extent = vk::Extent2D{cubeSize, cubeSize};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(cubeSize),
                              static_cast<float>(cubeSize), 0.0f, 1.0f};
        vk::Rect2D scissor{{0, 0}, {cubeSize, cubeSize}};
        cmd.setViewport(0, 1, &viewport);
        cmd.setScissor(0, 1, &scissor);
        cmd.setDepthBias(view.depthBias, 0.0f, view.slopeBias);

        if (prereq.hasMeshBatches)
        {
            bindShadowPipelineAndSets(cmd, ctx);

            ShadowPushConstants pc{};
            pc.lightViewProjection = view.viewProjectionMatrix;
            pc.depthBias = view.depthBias;
            pc.slopeBias = view.slopeBias;
            pc.normalBias = view.normalBias;

            dispatchMeshBatches(cmd, ctx, pc);
        }

        if (prereq.hasTerrainShadows)
        {
            ctx.terrainShadowPipeline->dispatch(
                cmd,
                ctx.terrainParams->terrainDataDescSet,
                ctx.terrainParams->terrainMeshletDescSet,
                ctx.terrainParams->terrainVertexDescSet,
                view.viewProjectionMatrix,
                ctx.terrainParams->tileCount,
                ctx.terrainParams->shadowLOD,
                view.depthBias, view.slopeBias);
        }

        cmd.endRenderPass();
    }

    std::vector<std::pair<uint32_t, LightShadowData*>>
        ShadowPassRecorder::collectPointLights(const ShadowPassContext& ctx) const
    {
        std::vector<std::pair<uint32_t, LightShadowData*>> result;
        for (auto& [entityId, data] : ctx.lightShadowData)
        {
            if (data.type != ShadowMapType::PointCube ||
                !data.settings.enabled || !data.settings.castShadows ||
                !data.resourceHandle.isValid())
                continue;
            if (data.isStatic && data.shadowCached)
                continue;

            ShadowCubeMap* cube = ctx.resourcePool->getCube(data.resourceHandle);
            if (!cube || !cube->isInitialized())
                continue;

            result.emplace_back(entityId, &data);
        }
        return result;
    }

    void ShadowPassRecorder::renderPointLightCubeShadows(
        vk::CommandBuffer cmd, const ShadowPassContext& ctx)
    {
        if (!ctx.resourcePool || !ctx.shadowPassPipeline)
            return;

        ShadowPassPrerequisites prereq;
        prereq.hasTerrainShadows = ctx.terrainParams != nullptr &&
                                   ctx.terrainParams->tileCount > 0 &&
                                   ctx.terrainShadowPipeline != nullptr &&
                                   ctx.terrainShadowPipeline->isInitialized();
        prereq.hasMeshBatches = ctx.params.batchCount > 0 &&
                                ctx.params.commandsPerSection > 0 &&
                                ctx.params.drawCommandBuffer &&
                                ctx.params.drawCountBuffer;

        if (!prereq.hasMeshBatches && !prereq.hasTerrainShadows)
            return;

        auto pointLightsToRender = collectPointLights(ctx);
        if (pointLightsToRender.empty())
            return;

        const auto& logicalDevice = device.getLogicalDevice();

        for (auto& [entityId, data] : pointLightsToRender)
        {
            ShadowCubeMap* cube = ctx.resourcePool->getCube(data->resourceHandle);
            uint32_t cubeSize = cube->getSize();

            cube->transitionToDepthAttachment(cmd);

            for (uint32_t face = 0; face < ShadowConstants::CUBE_FACE_COUNT; ++face)
            {
                if (face >= data->views.size())
                    continue;

                vk::Framebuffer fb = cube->getOrCreateFramebuffer(
                    face, ctx.shadowPassPipeline->getRenderPass(), logicalDevice);

                CubeFaceRenderInfo faceInfo{data->views[face], fb, cubeSize};
                renderCubeFace(cmd, ctx, prereq, faceInfo);
            }

            cube->transitionToShaderRead(cmd);
        }
    }
}
