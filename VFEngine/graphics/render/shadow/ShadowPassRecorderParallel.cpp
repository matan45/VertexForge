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

        bool hasPageViews = !pageRenderList.empty();
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

        if (hasPageViews)
        {
            transitionPoolToDepthAttachment(primaryCmd, tilePool, poolFirstUse);

            bool useLoadPass = !poolFirstUse;

            vk::ClearValue clearValue{};
            clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

            vk::RenderPassBeginInfo renderPassInfo{};
            vk::RenderPass renderPass;
            vk::Framebuffer framebuffer;

            if (useLoadPass)
            {
                renderPass = tilePool->getRenderPassLoad();
                framebuffer = tilePool->getFramebufferLoad();
                renderPassInfo.renderPass = renderPass;
                renderPassInfo.framebuffer = framebuffer;
                renderPassInfo.clearValueCount = 0;
                renderPassInfo.pClearValues = nullptr;
            }
            else
            {
                renderPass = tilePool->getRenderPass();
                framebuffer = tilePool->getFramebuffer();
                renderPassInfo.renderPass = renderPass;
                renderPassInfo.framebuffer = framebuffer;
                renderPassInfo.clearValueCount = 1;
                renderPassInfo.pClearValues = &clearValue;
            }
            renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
            renderPassInfo.renderArea.extent = vk::Extent2D{
                vsm::PHYSICAL_POOL_DIM, vsm::PHYSICAL_POOL_DIM};

            primaryCmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eSecondaryCommandBuffers);

            auto recordStart = std::chrono::high_resolution_clock::now();

            uint32_t tileCount = static_cast<uint32_t>(pageRenderList.size());
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
                        recordTileCommands(secondary, pageRenderList[i], tilePool,
                                          params, terrainParams, shadowPassPipeline,
                                          terrainShadowPipeline, hasMeshBatches,
                                          hasTerrainShadows, useLoadPass);
                    }

                    secondary.end();
                    secondaryBuffers[threadNum] = secondary;
                    threadUsed[threadNum] = true;
                }, 1);

            auto recordEnd = std::chrono::high_resolution_clock::now();

            std::vector<vk::CommandBuffer> validSecondaries;
            validSecondaries.reserve(threadCount);
            uint32_t usedThreads = 0;
            for (uint32_t t = 0; t < threadCount; t++)
            {
                if (threadUsed[t])
                {
                    validSecondaries.push_back(secondaryBuffers[t]);
                    usedThreads++;
                }
            }

            if (!validSecondaries.empty())
            {
                primaryCmd.executeCommands(
                    static_cast<uint32_t>(validSecondaries.size()),
                    validSecondaries.data());
            }

            lastStats.recordingUs = std::chrono::duration<float, std::micro>(recordEnd - recordStart).count();
            lastStats.tileCount = tileCount;
            lastStats.threadsUsed = usedThreads;
            lastStats.usedParallel = true;

            primaryCmd.endRenderPass();

            transitionPoolToShaderRead(primaryCmd, tilePool);
        }

        renderPointLightCubeShadows(primaryCmd, params, terrainParams, resourcePool,
                                     shadowPassPipeline, terrainShadowPipeline,
                                     lightShadowData);
    }
}
