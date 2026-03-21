#include "RenderPassHandler.hpp"
#include "print/Log.hpp"
#include "../core/SwapChain.hpp"
#include "../core/Device.hpp"
#include "ClearColor.hpp"
#include "IBL.hpp"
#include "DebugRenderer.hpp"
#include "mesh/StaticMeshPipeline.hpp"
#include "billboard/BillboardPipeline.hpp"
#include "text/TextPipeline.hpp"
#include "ui/UIRenderPipeline.hpp"
#include "ui/UITextPipeline.hpp"
#include "occlusion/CameraOcclusionManager.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "gpudriven/terrain/TerrainRaycastPipeline.hpp"
#include "postprocess/PostProcessPipeline.hpp"
#include "volumetric/VolumetricFogComposite.hpp"
#include "gi/SSGIPipeline.hpp"
#include "transparency/WBOITPipeline.hpp"
#include "decal/DecalPipeline.hpp"
#include "atmosphere/AtmospherePipeline.hpp"
#include "cloud/CloudPipeline.hpp"
#include "shadow/ShadowSystem.hpp"
#include "../../services/providers/vfx/IVFXRuntimeProvider.hpp"
#include "../../services/providers/terrain/ITerrainRenderProvider.hpp"
#include "../../services/providers/terrain/IWaterRenderProvider.hpp"
#include "../../services/providers/vegetation/IGrassRenderProvider.hpp"
#include "../../services/data/RenderHookContext.hpp"
#include "../../services/data/WaterData.hpp"
#include "water/WaterTypes.hpp"
#include "water/WaterTile.hpp"
#include "water/OceanFFT.hpp"
#include "vegetation/WindConfig.hpp"
#include <unordered_set>

namespace render
{
    void RenderPassHandler::updateGPUDrivenSceneData() const
    {
        if (!gpuDrivenRendererInitialized || !meshPipelineInitialized)
            return;

        gpuDrivenRenderer->updateScene(
            currentMeshDrawList, currentView, currentProjection,
            currentCameraPosition, currentNearPlane, currentFarPlane, currentTime);

        if (terrainRenderProvider && terrainRenderProvider->hasActiveTerrain() && currentFrustum)
        {
            if (terrainRenderProvider->consumeTerrainMaterialDirty())
                gpuDrivenRenderer->invalidateTerrainLayerData();

            auto visibleTiles = terrainRenderProvider->getVisibleTiles(*currentFrustum, currentCameraPosition);

            std::unordered_set<::terrain::TerrainTile*> terrainSeen(visibleTiles.begin(), visibleTiles.end());
            for (const auto& [rttFrustum, rttCameraPos] : additionalTerrainFrustums)
            {
                for (auto* tile : terrainRenderProvider->queryVisibleTiles(rttFrustum, rttCameraPos))
                {
                    if (terrainSeen.insert(tile).second)
                        visibleTiles.push_back(tile);
                }
            }

            glm::vec2 gridWorldMin(0.0f), gridWorldMax(0.0f);
            terrainRenderProvider->getTerrainGridWorldBounds(gridWorldMin, gridWorldMax);
            gpuDrivenRenderer->updateTerrain(visibleTiles, currentCameraPosition,
                                              terrainRenderProvider->getTerrainMaterialPath(),
                                              gridWorldMin, gridWorldMax);
            gpuDrivenRenderer->updateVegetationStreaming(visibleTiles,
                                                          terrainRenderProvider->getAllLoadedTiles(),
                                                          currentCameraPosition);

            if (grassRenderProvider)
            {
                auto grassConfig = grassRenderProvider->getGrassRenderConfig();
                gpuDrivenRenderer->setGrassRenderConfig(grassConfig);

                ::vegetation::WindConfig windConfig;
                windConfig.direction = grassConfig.windDirection;
                windConfig.speed = grassConfig.windSpeed * grassConfig.windStrength;
                windConfig.gustStrength = grassConfig.gustStrength;
                windConfig.gustFrequency = grassConfig.gustFrequency;
                gpuDrivenRenderer->updateWind(0.016f, windConfig);
            }
            else
            {
                gpuDrivenRenderer->updateWind(0.016f, ::vegetation::WindConfig{});
            }
        }

        if (waterRenderProvider && waterRenderProvider->hasActiveWater() && currentFrustum)
        {
            auto visibleTiles = waterRenderProvider->getVisibleWaterTiles(*currentFrustum, currentCameraPosition);

            std::unordered_set<::water::WaterTile*> waterSeen(visibleTiles.begin(), visibleTiles.end());
            for (const auto& [rttFrustum, rttCameraPos] : additionalWaterFrustums)
            {
                for (auto* tile : waterRenderProvider->queryVisibleWaterTiles(rttFrustum, rttCameraPos))
                {
                    if (waterSeen.insert(tile).second)
                        visibleTiles.push_back(tile);
                }
            }

            gpuDrivenRenderer->updateWater(visibleTiles,
                                            waterRenderProvider->getWaterGlobalSettings(),
                                            waterRenderProvider->getWaterTileConfig());
        }

        if (waterRenderProvider && gpuDrivenRenderer)
        {
            bool wantOcean = waterRenderProvider->isOceanFFTEnabled();
            uint32_t version = waterRenderProvider->getOceanFFTConfigVersion();

            auto buildOceanConfig = [this]() {
                auto cfgData = waterRenderProvider->getOceanFFTConfig();
                render::water::OceanFFTConfig cfg;
                cfg.resolution = cfgData.resolution;
                cfg.patchSize = cfgData.patchSize;
                cfg.windSpeed = cfgData.windSpeed;
                cfg.windDirection = cfgData.windDirection;
                cfg.amplitude = cfgData.amplitude;
                cfg.choppiness = cfgData.choppiness;
                cfg.gravity = waterRenderProvider->getPhysicsGravity();
                cfg.foamThreshold = cfgData.foamThreshold;
                cfg.displacementScale = cfgData.displacementScale;
                return cfg;
            };

            if (wantOcean && !oceanFFTInitialized)
            {
                gpuDrivenRenderer->initOceanFFT(buildOceanConfig());
                oceanFFTInitialized = true;
                lastOceanConfigVersion = version;

                auto* renderer = gpuDrivenRenderer.get();
                waterRenderProvider->setOceanHeightSampler(
                    [renderer](const glm::vec2& pos) { return renderer->getOceanHeightAt(pos); });
            }
            else if (!wantOcean && oceanFFTInitialized)
            {
                gpuDrivenRenderer->cleanupOceanFFT();
                oceanFFTInitialized = false;
                waterRenderProvider->setOceanHeightSampler(nullptr);
            }
            else if (wantOcean && oceanFFTInitialized && version != lastOceanConfigVersion)
            {
                gpuDrivenRenderer->updateOceanConfig(buildOceanConfig());
                lastOceanConfigVersion = version;
            }
        }

        additionalTerrainFrustums.clear();
        additionalWaterFrustums.clear();
    }

    void RenderPassHandler::executeOcclusionPasses(const vk::CommandBuffer& commandBuffer) const
    {
        occlusion::CameraId activeCameraId = cameraOcclusionManager->getActiveCameraId();

        if (!cameraOcclusionManager->isHiZInitialized(activeCameraId))
            return;

        cameraOcclusionManager->generateHiZ(activeCameraId, commandBuffer);

        if (terrainRaycastPipeline && terrainRaycastPipeline->isInitialized())
            dispatchTerrainRaycast(commandBuffer);

        if (gpuDrivenRendererInitialized && gpuDrivenRenderer->getShadowSystem() &&
            gpuDrivenRenderer->getShadowSystem()->isFeedbackEnabled())
        {
            auto* shadowSystem = gpuDrivenRenderer->getShadowSystem();
            auto extent = swapChain.getSwapchainExtent();
            glm::mat4 invVP = glm::inverse(currentProjection * currentView);

            {
                vk::ImageMemoryBarrier toShaderRead{};
                toShaderRead.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                toShaderRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toShaderRead.image = offscreenResources.depthImage.depthImage;
                toShaderRead.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth
                    | vk::ImageAspectFlagBits::eStencil;
                toShaderRead.subresourceRange.baseMipLevel = 0;
                toShaderRead.subresourceRange.levelCount = 1;
                toShaderRead.subresourceRange.baseArrayLayer = 0;
                toShaderRead.subresourceRange.layerCount = 1;
                toShaderRead.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
                toShaderRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;

                commandBuffer.pipelineBarrier(
                    vk::PipelineStageFlagBits::eLateFragmentTests,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, {}, {}, toShaderRead);
            }

            shadowSystem->dispatchFeedback(commandBuffer,
                offscreenResources.depthImage.depthImageView,
                invVP, extent.width, extent.height);
            shadowSystem->copyFeedbackToStaging(commandBuffer);

            {
                vk::ImageMemoryBarrier toAttachment{};
                toAttachment.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                toAttachment.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                toAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toAttachment.image = offscreenResources.depthImage.depthImage;
                toAttachment.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth
                    | vk::ImageAspectFlagBits::eStencil;
                toAttachment.subresourceRange.baseMipLevel = 0;
                toAttachment.subresourceRange.levelCount = 1;
                toAttachment.subresourceRange.baseArrayLayer = 0;
                toAttachment.subresourceRange.layerCount = 1;
                toAttachment.srcAccessMask = vk::AccessFlagBits::eShaderRead;
                toAttachment.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead
                    | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

                commandBuffer.pipelineBarrier(
                    vk::PipelineStageFlagBits::eComputeShader,
                    vk::PipelineStageFlagBits::eEarlyFragmentTests,
                    {}, {}, {}, toAttachment);
            }
        }
    }

    void RenderPassHandler::dispatchTerrainRaycast(const vk::CommandBuffer& commandBuffer) const
    {
        vk::ImageMemoryBarrier toShaderRead{};
        toShaderRead.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        toShaderRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead.image = offscreenResources.depthImage.depthImage;
        toShaderRead.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth
            | vk::ImageAspectFlagBits::eStencil;
        toShaderRead.subresourceRange.baseMipLevel = 0;
        toShaderRead.subresourceRange.levelCount = 1;
        toShaderRead.subresourceRange.baseArrayLayer = 0;
        toShaderRead.subresourceRange.layerCount = 1;
        toShaderRead.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        toShaderRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eLateFragmentTests,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {}, toShaderRead);

        glm::mat4 invViewProjection = glm::inverse(currentProjection * currentView);
        auto extent = swapChain.getSwapchainExtent();
        terrainRaycastPipeline->dispatch(commandBuffer, invViewProjection, extent.width, extent.height);
        terrainRaycastPipeline->copyResultsToStaging(commandBuffer);

        vk::ImageMemoryBarrier toAttachment{};
        toAttachment.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        toAttachment.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        toAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toAttachment.image = offscreenResources.depthImage.depthImage;
        toAttachment.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth
            | vk::ImageAspectFlagBits::eStencil;
        toAttachment.subresourceRange.baseMipLevel = 0;
        toAttachment.subresourceRange.levelCount = 1;
        toAttachment.subresourceRange.baseArrayLayer = 0;
        toAttachment.subresourceRange.layerCount = 1;
        toAttachment.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        toAttachment.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead
            | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eEarlyFragmentTests,
            {}, {}, {}, toAttachment);
    }

    void RenderPassHandler::executePostProcess(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        render::postprocess::CameraInfo camInfo{};
        camInfo.nearPlane = currentNearPlane;
        camInfo.farPlane = currentFarPlane;
        camInfo.cameraPosition = currentCameraPosition;
        camInfo.viewMatrix = currentView;
        camInfo.projectionMatrix = currentProjection;
        camInfo.unjitteredProjectionMatrix = unjitteredProjection;
        camInfo.jitterOffset = currentJitterOffset;
        camInfo.frameIndex = taaFrameIndex;
        camInfo.time = currentTime;
        postProcessPipeline->setCameraData(camInfo);

        if (gpuDrivenRendererInitialized)
            updateSunScreenPosition();

        postProcessPipeline->execute(commandBuffer, imageIndex);
    }

    void RenderPassHandler::updateSunScreenPosition() const
    {
        auto* lbm = gpuDrivenRenderer->getLightBufferManager();
        auto sunDir = lbm->getFirstDirectionalLightDirection();
        if (!sunDir)
        {
            postProcessPipeline->setSunData({0.5f, 0.5f}, false);
            return;
        }

        glm::vec3 sunWorldPos = currentCameraPosition - (*sunDir) * currentFarPlane;
        glm::vec4 clip = currentProjection * currentView * glm::vec4(sunWorldPos, 1.0f);
        if (clip.w > 0.0f)
        {
            glm::vec2 screenUV = (glm::vec2(clip) / clip.w) * 0.5f + 0.5f;
            postProcessPipeline->setSunData(screenUV, true);
        }
        else
        {
            postProcessPipeline->setSunData({0.5f, 0.5f}, false);
        }
    }

    void RenderPassHandler::drawUIOverlays(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        if (uiPipelineInitialized && !currentUIImageDrawList.empty())
            uiPipeline->recordCommandBuffer(commandBuffer, imageIndex);

        if (uiTextPipelineInitialized && !currentUITextDrawList.empty())
            uiTextPipeline->recordCommandBuffer(commandBuffer, imageIndex);
    }

    plugin::RenderHookHandle RenderPassHandler::registerRenderHook(
        plugin::RenderPassHookPoint hookPoint,
        plugin::RenderHookCallback callback)
    {
        plugin::RenderHookHandle handle;
        handle.id = nextRenderHookId++;

        RegisteredRenderHook hook;
        hook.handle = handle;
        hook.hookPoint = hookPoint;
        hook.callback = std::move(callback);
        renderHooks.push_back(std::move(hook));

        return handle;
    }

    void RenderPassHandler::unregisterRenderHook(plugin::RenderHookHandle handle)
    {
        std::erase_if(renderHooks,
            [&](const RegisteredRenderHook& h) { return h.handle.id == handle.id; });
    }

    void RenderPassHandler::executeRenderHooks(
        plugin::RenderPassHookPoint hookPoint,
        const vk::CommandBuffer& commandBuffer,
        uint32_t imageIndex) const
    {
        for (const auto& hook : renderHooks)
        {
            if (hook.hookPoint != hookPoint) continue;

            plugin::RenderHookContext ctx{};
            ctx.commandBuffer    = commandBuffer;
            ctx.imageIndex       = imageIndex;
            auto extent = swapChain.getSwapchainExtent();
            ctx.viewportWidth    = extent.width;
            ctx.viewportHeight   = extent.height;
            ctx.renderPass       = meshPipelineInitialized ? meshPipeline->getRenderPass() : vk::RenderPass{};
            ctx.device           = device.getLogicalDevice();
            ctx.viewMatrix       = currentView;
            ctx.projectionMatrix = currentProjection;
            ctx.cameraPosition   = currentCameraPosition;
            ctx.nearPlane        = currentNearPlane;
            ctx.farPlane         = currentFarPlane;
            ctx.time             = currentTime;

            try
            {
                hook.callback(ctx);
            }
            catch (const std::exception& e)
            {
                vfLogError("Plugin render hook error: {}", e.what());
            }
        }
    }

    void RenderPassHandler::recordAsyncCompute(vk::CommandBuffer asyncCmd) const
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer->isEnabled())
            gpuDrivenRenderer->dispatchAsyncCompute(asyncCmd);

        if (atmospherePipeline && atmospherePipeline->isEnabled())
        {
            atmospherePipeline->setCameraData(currentView, currentProjection,
                                               currentCameraPosition,
                                               currentNearPlane, currentFarPlane);
            if (gpuDrivenRendererInitialized)
            {
                auto* lbm = gpuDrivenRenderer->getLightBufferManager();
                auto sunDir = lbm->getFirstDirectionalLightDirection();
                if (sunDir)
                    atmospherePipeline->setSunDirection(*sunDir);
            }
            atmospherePipeline->dispatchCompute(asyncCmd);
        }

        if (cloudPipeline && cloudPipeline->isEnabled())
        {
            cloudPipeline->setCameraData(currentView, currentProjection,
                                          currentCameraPosition,
                                          currentNearPlane, currentFarPlane,
                                          currentTime);
            if (gpuDrivenRendererInitialized)
            {
                auto* lbm = gpuDrivenRenderer->getLightBufferManager();
                auto sunDir = lbm->getFirstDirectionalLightDirection();
                if (sunDir)
                    cloudPipeline->setSunDirection(*sunDir);
            }
            if (atmospherePipeline && atmospherePipeline->isInitialized())
            {
                auto atmosSettings = atmospherePipeline->getSettings();
                cloudPipeline->setSunIrradiance(atmosSettings.sunIrradiance);
            }
            cloudPipeline->dispatchCompute(asyncCmd);
        }

        if (vfxRuntimeProvider)
            vfxRuntimeProvider->recordComputeCommands(asyncCmd);
    }
}
