#include "ShadowPassRecorder.hpp"
#include "VSMPhysicalTilePool.hpp"
#include "ShadowResourcePool.hpp"
#include "ShadowPassPipeline.hpp"
#include "TerrainShadowPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/ThreadCommandPoolManager.hpp"
#include "print/Log.hpp"
#include "threading/JobSystem.hpp"
#include <chrono>

namespace render::shadow
{
    ShadowPassRecorder::ShadowPassRecorder(core::Device& device)
        : device(device)
    {
    }

    void ShadowPassRecorder::recordShadowPass(
        vk::CommandBuffer cmd,
        const ShadowPassParams& params,
        const TerrainShadowPassParams* terrainParams,
        VSMPhysicalTilePool* tilePool,
        ShadowResourcePool* resourcePool,
        ShadowPassPipeline* shadowPassPipeline,
        TerrainShadowPipeline* terrainShadowPipeline,
        const std::vector<PageRenderEntry>& pageRenderList,
        std::unordered_map<uint32_t, LightShadowData>& lightShadowData,
        bool shadowsEnabled,
        bool poolFirstUse)
    {
        if (!shadowsEnabled || !shadowPassPipeline || !shadowPassPipeline->isInitialized() || !tilePool)
        {
            lastStats = {};
            return;
        }
        auto recordStart = std::chrono::high_resolution_clock::now();

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

        // Render VSM page-based shadows into the physical tile pool
        if (hasPageViews)
        {
            // Transition pool image for rendering
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

                cmd.pipelineBarrier(
                    srcStage,
                    vk::PipelineStageFlagBits::eEarlyFragmentTests,
                    {},
                    0, nullptr,
                    0, nullptr,
                    1, &barrier
                );
            }

            // Use eLoad render pass to preserve cached tiles (clear individual tiles via clearAttachments)
            bool useLoadPass = !poolFirstUse;

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
                vsm::PHYSICAL_POOL_DIM, vsm::PHYSICAL_POOL_DIM
            };

            cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

            if (hasMeshBatches)
            {
                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, shadowPassPipeline->getPipeline());

                std::array<vk::DescriptorSet, 5> descriptorSets = {
                    params.perDrawDataDescSet,
                    params.meshletDataDescSet,
                    params.vertexDataDescSet,
                    params.boneMatrixDescSet,
                    params.cameraDescSet
                };
                cmd.bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics,
                    shadowPassPipeline->getPipelineLayout(),
                    0,
                    static_cast<uint32_t>(descriptorSets.size()),
                    descriptorSets.data(),
                    0, nullptr
                );
            }

            for (const auto& page : pageRenderList)
            {
                vk::Viewport viewport = tilePool->getTileViewport(page.physicalTileIndex);
                cmd.setViewport(0, 1, &viewport);

                vk::Rect2D scissor = tilePool->getTileScissor(page.physicalTileIndex);
                cmd.setScissor(0, 1, &scissor);

                // Clear just this tile's region when using eLoad pass
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

                if (hasMeshBatches)
                {
                    ShadowPushConstants pc{};
                    pc.lightViewProjection = page.cropViewProjection;
                    pc.baseDrawIndex = 0;
                    pc.depthBias = page.depthBias;
                    pc.slopeBias = page.slopeBias;
                    pc.normalBias = page.normalBias;

                    for (uint32_t shaderGroup = 0; shaderGroup < params.shaderGroupCount; ++shaderGroup)
                    {
                        if (shaderGroup == params.transparentGroupIndex) continue;

                        for (uint32_t batch = 0; batch < params.batchCount; ++batch)
                        {
                            uint32_t sectionIndex = batch * params.shaderGroupCount + shaderGroup;
                            pc.baseDrawIndex = sectionIndex * params.commandsPerSection;

                            cmd.pushConstants(
                                shadowPassPipeline->getPipelineLayout(),
                                vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
                                0,
                                sizeof(ShadowPushConstants),
                                &pc
                            );

                            vk::DeviceSize commandOffset = sectionIndex * params.commandsPerSection * sizeof(
                                vk::DrawMeshTasksIndirectCommandEXT);
                            vk::DeviceSize countOffset = sectionIndex * params.drawCountStructSize;

                            cmd.drawMeshTasksIndirectCountEXT(
                                params.drawCommandBuffer,
                                commandOffset,
                                params.drawCountBuffer,
                                countOffset,
                                params.commandsPerSection,
                                sizeof(vk::DrawMeshTasksIndirectCommandEXT)
                            );
                        }
                    }
                }

                // Terrain dispatch per page — the terrain task shader frustum-culls
                // against the crop VP, so most tiles are rejected for narrow pages.
                // Dispatch overhead is higher than old per-view approach but geometry
                // cost is comparable due to tighter culling.
                if (hasTerrainShadows)
                {
                    constexpr float terrainBiasScale = 4.0f;
                    terrainShadowPipeline->dispatch(
                        cmd,
                        terrainParams->terrainDataDescSet,
                        terrainParams->terrainMeshletDescSet,
                        terrainParams->terrainVertexDescSet,
                        page.cropViewProjection,
                        terrainParams->tileCount,
                        terrainParams->shadowLOD,
                        page.depthBias * terrainBiasScale,
                        page.slopeBias * terrainBiasScale
                    );
                }
            }

            cmd.endRenderPass();

            // Transition back to shader read
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

                cmd.pipelineBarrier(
                    vk::PipelineStageFlagBits::eLateFragmentTests,
                    vk::PipelineStageFlagBits::eFragmentShader,
                    {},
                    0, nullptr,
                    0, nullptr,
                    1, &barrier
                );
            }
        }

        auto recordEnd = std::chrono::high_resolution_clock::now();
        lastStats.recordingUs = std::chrono::duration<float, std::micro>(recordEnd - recordStart).count();
        lastStats.tileCount = static_cast<uint32_t>(pageRenderList.size());
        lastStats.threadsUsed = 0;
        lastStats.usedParallel = false;

        renderPointLightCubeShadows(cmd, params, terrainParams, resourcePool,
                                     shadowPassPipeline, terrainShadowPipeline,
                                     lightShadowData);
    }

    void ShadowPassRecorder::renderPointLightCubeShadows(
        vk::CommandBuffer cmd,
        const ShadowPassParams& params,
        const TerrainShadowPassParams* terrainParams,
        ShadowResourcePool* resourcePool,
        ShadowPassPipeline* shadowPassPipeline,
        TerrainShadowPipeline* terrainShadowPipeline,
        std::unordered_map<uint32_t, LightShadowData>& lightShadowData)
    {
        if (!resourcePool || !shadowPassPipeline)
            return;

        bool hasTerrainShadows = terrainParams != nullptr &&
                                  terrainParams->tileCount > 0 &&
                                  terrainShadowPipeline != nullptr &&
                                  terrainShadowPipeline->isInitialized();

        const auto& logicalDevice = device.getLogicalDevice();

        std::vector<std::pair<uint32_t, LightShadowData*>> pointLightsToRender;

        for (auto& [entityId, data] : lightShadowData)
        {
            if (data.type != ShadowMapType::PointCube ||
                !data.settings.enabled || !data.settings.castShadows ||
                !data.resourceHandle.isValid())
                continue;

            if (data.isStatic && data.shadowCached)
                continue;

            ShadowCubeMap* cube = resourcePool->getCube(data.resourceHandle);
            if (!cube || !cube->isInitialized())
                continue;

            pointLightsToRender.emplace_back(entityId, &data);
        }

        if (pointLightsToRender.empty())
            return;

        bool hasMeshBatches = params.batchCount > 0 &&
                              params.commandsPerSection > 0 &&
                              params.drawCommandBuffer &&
                              params.drawCountBuffer;

        if (!hasMeshBatches && !hasTerrainShadows)
            return;

        std::array<vk::DescriptorSet, 5> meshDescriptorSets = {
            params.perDrawDataDescSet,
            params.meshletDataDescSet,
            params.vertexDataDescSet,
            params.boneMatrixDescSet,
            params.cameraDescSet
        };

        for (auto& [entityId, data] : pointLightsToRender)
        {
            ShadowCubeMap* cube = resourcePool->getCube(data->resourceHandle);
            uint32_t cubeSize = cube->getSize();

            cube->transitionToDepthAttachment(cmd);

            vk::Viewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(cubeSize);
            viewport.height = static_cast<float>(cubeSize);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            vk::Rect2D scissor{};
            scissor.offset = vk::Offset2D{0, 0};
            scissor.extent = vk::Extent2D{cubeSize, cubeSize};

            for (uint32_t face = 0; face < ShadowConstants::CUBE_FACE_COUNT; ++face)
            {
                if (face >= data->views.size())
                    continue;

                const auto& view = data->views[face];

                vk::Framebuffer faceFramebuffer = cube->getOrCreateFramebuffer(
                    face, shadowPassPipeline->getRenderPass(), logicalDevice);

                vk::ClearValue clearValue{};
                clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

                vk::RenderPassBeginInfo renderPassInfo{};
                renderPassInfo.renderPass = shadowPassPipeline->getRenderPass();
                renderPassInfo.framebuffer = faceFramebuffer;
                renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
                renderPassInfo.renderArea.extent = vk::Extent2D{cubeSize, cubeSize};
                renderPassInfo.clearValueCount = 1;
                renderPassInfo.pClearValues = &clearValue;

                cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

                cmd.setViewport(0, 1, &viewport);
                cmd.setScissor(0, 1, &scissor);
                cmd.setDepthBias(view.depthBias, 0.0f, view.slopeBias);

                if (hasMeshBatches)
                {
                    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, shadowPassPipeline->getPipeline());
                    cmd.bindDescriptorSets(
                        vk::PipelineBindPoint::eGraphics,
                        shadowPassPipeline->getPipelineLayout(),
                        0,
                        static_cast<uint32_t>(meshDescriptorSets.size()),
                        meshDescriptorSets.data(),
                        0, nullptr
                    );

                    ShadowPushConstants pc{};
                    pc.lightViewProjection = view.viewProjectionMatrix;
                    pc.depthBias = view.depthBias;
                    pc.slopeBias = view.slopeBias;
                    pc.normalBias = view.normalBias;

                    for (uint32_t shaderGroup = 0; shaderGroup < params.shaderGroupCount; ++shaderGroup)
                    {
                        if (shaderGroup == params.transparentGroupIndex) continue;

                        for (uint32_t batch = 0; batch < params.batchCount; ++batch)
                        {
                            uint32_t sectionIndex = batch * params.shaderGroupCount + shaderGroup;
                            pc.baseDrawIndex = sectionIndex * params.commandsPerSection;

                            cmd.pushConstants(
                                shadowPassPipeline->getPipelineLayout(),
                                vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
                                0,
                                sizeof(ShadowPushConstants),
                                &pc
                            );

                            vk::DeviceSize commandOffset = sectionIndex * params.commandsPerSection * sizeof(
                                vk::DrawMeshTasksIndirectCommandEXT);
                            vk::DeviceSize countOffset = sectionIndex * params.drawCountStructSize;

                            cmd.drawMeshTasksIndirectCountEXT(
                                params.drawCommandBuffer,
                                commandOffset,
                                params.drawCountBuffer,
                                countOffset,
                                params.commandsPerSection,
                                sizeof(vk::DrawMeshTasksIndirectCommandEXT)
                            );
                        }
                    }
                }

                if (hasTerrainShadows)
                {
                    terrainShadowPipeline->dispatch(
                        cmd,
                        terrainParams->terrainDataDescSet,
                        terrainParams->terrainMeshletDescSet,
                        terrainParams->terrainVertexDescSet,
                        view.viewProjectionMatrix,
                        terrainParams->tileCount,
                        terrainParams->shadowLOD,
                        view.depthBias,
                        view.slopeBias
                    );
                }

                cmd.endRenderPass();
            }

            cube->transitionToShaderRead(cmd);
        }
    }

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

        // Render VSM page-based shadows with parallel tile recording
        if (hasPageViews)
        {
            // Transition pool image for rendering (on primary)
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

                primaryCmd.pipelineBarrier(
                    srcStage,
                    vk::PipelineStageFlagBits::eEarlyFragmentTests,
                    {},
                    0, nullptr,
                    0, nullptr,
                    1, &barrier
                );
            }

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
                vsm::PHYSICAL_POOL_DIM, vsm::PHYSICAL_POOL_DIM
            };

            // Begin render pass with SECONDARY command buffer support
            primaryCmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eSecondaryCommandBuffers);

            // Record tiles in parallel across worker threads
            auto recordStart = std::chrono::high_resolution_clock::now();

            uint32_t tileCount = static_cast<uint32_t>(pageRenderList.size());
            uint32_t threadCount = threadPoolManager->getThreadCount();

            // Collect secondary buffers from each thread (indexed by threadNum)
            std::vector<vk::CommandBuffer> secondaryBuffers(threadCount, nullptr);
            std::vector<bool> threadUsed(threadCount, false);

            // enkiTS guarantees each concurrent invocation gets a unique threadNum
            // in range [0, threadCount). Each thread writes only its own slot.
            threading::JobSystem::instance().parallelFor(tileCount,
                [&](uint32_t begin, uint32_t end, uint32_t threadNum) {
                    vk::CommandBuffer secondary = threadPoolManager->getSecondary(threadNum, frameIndex);

                    // Begin secondary with render pass inheritance
                    vk::CommandBufferInheritanceInfo inheritance{};
                    inheritance.renderPass = renderPass;
                    inheritance.subpass = 0;
                    inheritance.framebuffer = framebuffer;

                    vk::CommandBufferBeginInfo beginInfo{};
                    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit |
                                     vk::CommandBufferUsageFlagBits::eRenderPassContinue;
                    beginInfo.pInheritanceInfo = &inheritance;
                    secondary.begin(beginInfo);

                    // Bind pipeline + descriptors (must rebind per secondary buffer)
                    if (hasMeshBatches)
                    {
                        secondary.bindPipeline(vk::PipelineBindPoint::eGraphics, shadowPassPipeline->getPipeline());

                        std::array<vk::DescriptorSet, 5> descriptorSets = {
                            params.perDrawDataDescSet,
                            params.meshletDataDescSet,
                            params.vertexDataDescSet,
                            params.boneMatrixDescSet,
                            params.cameraDescSet
                        };
                        secondary.bindDescriptorSets(
                            vk::PipelineBindPoint::eGraphics,
                            shadowPassPipeline->getPipelineLayout(),
                            0,
                            static_cast<uint32_t>(descriptorSets.size()),
                            descriptorSets.data(),
                            0, nullptr
                        );
                    }

                    // Record assigned tiles
                    for (uint32_t i = begin; i < end; ++i)
                    {
                        const auto& page = pageRenderList[i];

                        vk::Viewport viewport = tilePool->getTileViewport(page.physicalTileIndex);
                        secondary.setViewport(0, 1, &viewport);

                        vk::Rect2D scissor = tilePool->getTileScissor(page.physicalTileIndex);
                        secondary.setScissor(0, 1, &scissor);

                        if (useLoadPass)
                        {
                            vk::ClearAttachment clearAttach{};
                            clearAttach.aspectMask = vk::ImageAspectFlagBits::eDepth;
                            clearAttach.clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

                            vk::ClearRect clearRect{};
                            clearRect.rect = scissor;
                            clearRect.baseArrayLayer = 0;
                            clearRect.layerCount = 1;

                            secondary.clearAttachments(1, &clearAttach, 1, &clearRect);
                        }

                        secondary.setDepthBias(page.depthBias, 0.0f, page.slopeBias);

                        if (hasMeshBatches)
                        {
                            ShadowPushConstants pc{};
                            pc.lightViewProjection = page.cropViewProjection;
                            pc.baseDrawIndex = 0;
                            pc.depthBias = page.depthBias;
                            pc.slopeBias = page.slopeBias;
                            pc.normalBias = page.normalBias;

                            for (uint32_t shaderGroup = 0; shaderGroup < params.shaderGroupCount; ++shaderGroup)
                            {
                                if (shaderGroup == params.transparentGroupIndex) continue;

                                for (uint32_t batch = 0; batch < params.batchCount; ++batch)
                                {
                                    uint32_t sectionIndex = batch * params.shaderGroupCount + shaderGroup;
                                    pc.baseDrawIndex = sectionIndex * params.commandsPerSection;

                                    secondary.pushConstants(
                                        shadowPassPipeline->getPipelineLayout(),
                                        vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
                                        0,
                                        sizeof(ShadowPushConstants),
                                        &pc
                                    );

                                    vk::DeviceSize commandOffset = sectionIndex * params.commandsPerSection * sizeof(
                                        vk::DrawMeshTasksIndirectCommandEXT);
                                    vk::DeviceSize countOffset = sectionIndex * params.drawCountStructSize;

                                    secondary.drawMeshTasksIndirectCountEXT(
                                        params.drawCommandBuffer,
                                        commandOffset,
                                        params.drawCountBuffer,
                                        countOffset,
                                        params.commandsPerSection,
                                        sizeof(vk::DrawMeshTasksIndirectCommandEXT)
                                    );
                                }
                            }
                        }

                        if (hasTerrainShadows)
                        {
                            constexpr float terrainBiasScale = 4.0f;
                            terrainShadowPipeline->dispatch(
                                secondary,
                                terrainParams->terrainDataDescSet,
                                terrainParams->terrainMeshletDescSet,
                                terrainParams->terrainVertexDescSet,
                                page.cropViewProjection,
                                terrainParams->tileCount,
                                terrainParams->shadowLOD,
                                page.depthBias * terrainBiasScale,
                                page.slopeBias * terrainBiasScale
                            );
                        }
                    }

                    secondary.end();
                    secondaryBuffers[threadNum] = secondary;
                    threadUsed[threadNum] = true;
                }, 1  // minBatchSize=1 (each tile has significant draw work)
            );

            auto recordEnd = std::chrono::high_resolution_clock::now();

            // Assemble secondary buffers into primary (in thread order)
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
                    validSecondaries.data()
                );
            }

            // Update stats
            lastStats.recordingUs = std::chrono::duration<float, std::micro>(recordEnd - recordStart).count();
            lastStats.tileCount = tileCount;
            lastStats.threadsUsed = usedThreads;
            lastStats.usedParallel = true;

            primaryCmd.endRenderPass();

            // Transition back to shader read
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

                primaryCmd.pipelineBarrier(
                    vk::PipelineStageFlagBits::eLateFragmentTests,
                    vk::PipelineStageFlagBits::eFragmentShader,
                    {},
                    0, nullptr,
                    0, nullptr,
                    1, &barrier
                );
            }
        }

        // Point light cube shadows stay inline (separate render passes per face)
        renderPointLightCubeShadows(primaryCmd, params, terrainParams, resourcePool,
                                     shadowPassPipeline, terrainShadowPipeline,
                                     lightShadowData);
    }
}
