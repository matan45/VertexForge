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
    void ShadowPassRecorder::recordShadowPassParallel(
        vk::CommandBuffer primaryCmd,
        const ShadowPassParams& params,
        const TerrainShadowPassParams* terrainParams,
        VSMPhysicalTilePool* tilePool,
        ShadowResourcePool* resourcePool,
        ShadowPassPipeline* shadowPassPipeline,
        TerrainShadowPipeline* terrainShadowPipeline,
        const std::vector<PageRenderEntry>& pageRenderList,
        const std::vector<PageRenderEntry>& staticPageRenderList,
        const std::vector<PageRenderEntry>& dynamicPageRenderList,
        const std::vector<TileCopyEntry>& tileCopyList,
        std::unordered_map<uint32_t, LightShadowData>& lightShadowData,
        bool shadowsEnabled,
        bool poolFirstUse,
        core::ThreadCommandPoolManager* threadPoolManager,
        uint32_t frameIndex)
    {
        if (!shadowsEnabled || !shadowPassPipeline || !shadowPassPipeline->isInitialized() || !tilePool)
        {
            lastStats = {};
            return;
        }

        bool hasTerrainShadows = terrainParams != nullptr &&
                                  terrainParams->tileCount > 0 &&
                                  terrainShadowPipeline != nullptr &&
                                  terrainShadowPipeline->isInitialized();

        bool hasPageViews = !pageRenderList.empty() || !staticPageRenderList.empty() || !dynamicPageRenderList.empty();
        bool hasPointShadows = false;
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (data.type == ShadowMapType::PointCube &&
                data.settings.enabled && data.settings.castShadows &&
                data.resourceHandle.isValid())
            {
                hasPointShadows = true;
                break;
            }
        }

        if (!hasPageViews && !hasPointShadows)
            return;

        bool hasMeshBatches = params.batchCount > 0 &&
                              params.commandsPerSection > 0 &&
                              params.drawCommandBuffer &&
                              params.drawCountBuffer;

        if (!hasMeshBatches && !hasTerrainShadows)
            return;

        // Lambda to dispatch pages across threads using secondary command buffers
        auto dispatchPagesParallel = [&](const std::vector<PageRenderEntry>& pages,
                                         vk::RenderPass renderPass, vk::Framebuffer framebuffer,
                                         bool useLoadPass)
        {
            uint32_t tileCount = static_cast<uint32_t>(pages.size());
            uint32_t threadCount = threadPoolManager->getThreadCount();

            std::vector<vk::CommandBuffer> secondaryBuffers(threadCount, nullptr);
            std::vector<bool> threadUsed(threadCount, false);

            threading::JobSystem::instance().parallelFor(tileCount,
                [&](uint32_t begin, uint32_t end, uint32_t threadNum) {
                    vk::CommandBuffer secondary = threadPoolManager->getSecondary(threadNum, frameIndex);

                    vk::CommandBufferInheritanceInfo inheritance{};
                    inheritance.renderPass = renderPass;
                    inheritance.subpass = 0;
                    inheritance.framebuffer = framebuffer;

                    vk::CommandBufferBeginInfo beginInfo{};
                    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit |
                                     vk::CommandBufferUsageFlagBits::eRenderPassContinue;
                    beginInfo.pInheritanceInfo = &inheritance;
                    secondary.begin(beginInfo);

                    if (hasMeshBatches)
                    {
                        secondary.bindPipeline(vk::PipelineBindPoint::eGraphics, shadowPassPipeline->getPipeline());

                        std::array<vk::DescriptorSet, 5> descriptorSets = {
                            params.perDrawDataDescSet, params.meshletDataDescSet,
                            params.vertexDataDescSet, params.boneMatrixDescSet,
                            params.cameraDescSet
                        };
                        secondary.bindDescriptorSets(
                            vk::PipelineBindPoint::eGraphics,
                            shadowPassPipeline->getPipelineLayout(),
                            0, static_cast<uint32_t>(descriptorSets.size()),
                            descriptorSets.data(), 0, nullptr);
                    }

                    for (uint32_t i = begin; i < end; ++i)
                    {
                        recordTileCommands(secondary, pages[i], tilePool,
                                          params, terrainParams, shadowPassPipeline,
                                          terrainShadowPipeline, hasMeshBatches,
                                          hasTerrainShadows, useLoadPass);
                    }

                    secondary.end();
                    secondaryBuffers[threadNum] = secondary;
                    threadUsed[threadNum] = true;
                }, 1);

            std::vector<vk::CommandBuffer> validSecondaries;
            validSecondaries.reserve(threadCount);
            for (uint32_t t = 0; t < threadCount; t++)
            {
                if (threadUsed[t])
                    validSecondaries.push_back(secondaryBuffers[t]);
            }

            if (!validSecondaries.empty())
            {
                primaryCmd.executeCommands(
                    static_cast<uint32_t>(validSecondaries.size()),
                    validSecondaries.data());
            }
        };

        if (hasPageViews)
        {
            transitionPoolToDepthAttachment(primaryCmd, tilePool, poolFirstUse);
            bool useLoadPass = !poolFirstUse;

            auto recordStart = std::chrono::high_resolution_clock::now();

            // Phase A: Legacy (static lights) + static-layer pages
            bool hasLegacyOrStaticPages = !pageRenderList.empty() || !staticPageRenderList.empty();
            if (hasLegacyOrStaticPages)
            {
                vk::ClearValue clearValue{};
                clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

                vk::RenderPass rp;
                vk::Framebuffer fb;
                vk::RenderPassBeginInfo rpInfo{};

                if (useLoadPass)
                {
                    rp = tilePool->getRenderPassLoad();
                    fb = tilePool->getFramebufferLoad();
                    rpInfo.renderPass = rp;
                    rpInfo.framebuffer = fb;
                    rpInfo.clearValueCount = 0;
                    rpInfo.pClearValues = nullptr;
                }
                else
                {
                    rp = tilePool->getRenderPass();
                    fb = tilePool->getFramebuffer();
                    rpInfo.renderPass = rp;
                    rpInfo.framebuffer = fb;
                    rpInfo.clearValueCount = 1;
                    rpInfo.pClearValues = &clearValue;
                }
                rpInfo.renderArea.offset = vk::Offset2D{0, 0};
                rpInfo.renderArea.extent = vk::Extent2D{vsm::PHYSICAL_POOL_DIM, vsm::PHYSICAL_POOL_DIM};

                // Combine legacy + static pages
                std::vector<PageRenderEntry> combinedPhaseA;
                combinedPhaseA.reserve(pageRenderList.size() + staticPageRenderList.size());
                combinedPhaseA.insert(combinedPhaseA.end(), pageRenderList.begin(), pageRenderList.end());
                combinedPhaseA.insert(combinedPhaseA.end(), staticPageRenderList.begin(), staticPageRenderList.end());

                primaryCmd.beginRenderPass(rpInfo, vk::SubpassContents::eSecondaryCommandBuffers);
                dispatchPagesParallel(combinedPhaseA, rp, fb, useLoadPass);
                primaryCmd.endRenderPass();
            }

            // Phase B: Copy static tiles -> dynamic tiles
            if (!tileCopyList.empty())
            {
                VSMPhysicalTilePool::transitionPoolToTransfer(primaryCmd, tilePool);

                std::vector<vk::ImageCopy> copyRegions;
                copyRegions.reserve(tileCopyList.size());
                for (const auto& entry : tileCopyList)
                {
                    copyRegions.push_back(tilePool->getTileCopyRegion(entry.srcTileIndex, entry.dstTileIndex));
                }

                primaryCmd.copyImage(
                    tilePool->getPoolImage(), vk::ImageLayout::eGeneral,
                    tilePool->getPoolImage(), vk::ImageLayout::eGeneral,
                    static_cast<uint32_t>(copyRegions.size()), copyRegions.data());

                VSMPhysicalTilePool::transitionPoolFromTransfer(primaryCmd, tilePool);
            }

            // Phase C: Dynamic-layer pages
            if (!dynamicPageRenderList.empty())
            {
                vk::RenderPass rp = tilePool->getRenderPassLoad();
                vk::Framebuffer fb = tilePool->getFramebufferLoad();

                vk::RenderPassBeginInfo rpInfo{};
                rpInfo.renderPass = rp;
                rpInfo.framebuffer = fb;
                rpInfo.clearValueCount = 0;
                rpInfo.pClearValues = nullptr;
                rpInfo.renderArea.offset = vk::Offset2D{0, 0};
                rpInfo.renderArea.extent = vk::Extent2D{vsm::PHYSICAL_POOL_DIM, vsm::PHYSICAL_POOL_DIM};

                primaryCmd.beginRenderPass(rpInfo, vk::SubpassContents::eSecondaryCommandBuffers);
                dispatchPagesParallel(dynamicPageRenderList, rp, fb, true);
                primaryCmd.endRenderPass();
            }

            auto recordEnd = std::chrono::high_resolution_clock::now();

            lastStats.recordingUs = std::chrono::duration<float, std::micro>(recordEnd - recordStart).count();
            lastStats.tileCount = static_cast<uint32_t>(pageRenderList.size() + staticPageRenderList.size() + dynamicPageRenderList.size());
            lastStats.threadsUsed = threadPoolManager->getThreadCount();
            lastStats.usedParallel = true;

            transitionPoolToShaderRead(primaryCmd, tilePool);
        }

        renderPointLightCubeShadows(primaryCmd, params, terrainParams, resourcePool,
                                     shadowPassPipeline, terrainShadowPipeline,
                                     lightShadowData);
    }
}
