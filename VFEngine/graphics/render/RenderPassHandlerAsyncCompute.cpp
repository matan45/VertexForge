#include "RenderPassHandler.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
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
#include "upscaling/UpscaleManager.hpp"
#include "upscaling/MotionVectorPass.hpp"
#include "../core/ImageUtilities.hpp"
#include "time/Timer.hpp"
#include "shadow/ShadowSystem.hpp"
#include "../../services/providers/vfx/IVFXRuntimeProvider.hpp"
#include "../../services/providers/terrain/ITerrainRenderProvider.hpp"
#include "../../services/providers/terrain/IOceanRenderProvider.hpp"
#include "../../services/data/OceanData.hpp"
#include "../../services/providers/vegetation/IGrassRenderProvider.hpp"
#include "../../services/data/RenderHookContext.hpp"
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

        if (oceanRenderProvider && oceanRenderProvider->hasActiveOcean() && gpuDrivenRenderer)
        {
            oceanRenderProvider->processWaterTileStreaming();

            auto visualSettings = oceanRenderProvider->getOceanVisualSettings();
            float baseHeight = oceanRenderProvider->getBaseWaterHeight();
            auto cfgData = oceanRenderProvider->getOceanFFTConfig();
            const auto* tileGrid = oceanRenderProvider->getWaterTileGrid();
            bool worldMode = oceanRenderProvider->isWorldModeActive();
            gpuDrivenRenderer->updateWater(visualSettings, baseHeight,
                                            currentCameraPosition, cfgData.bands[0].patchSize,
                                            worldMode, tileGrid);
        }

        if (oceanRenderProvider && gpuDrivenRenderer)
        {
            bool wantOcean = oceanRenderProvider->isOceanFFTEnabled();
            uint32_t version = oceanRenderProvider->getOceanFFTConfigVersion();

            auto buildOceanConfigs = [this]() -> std::pair<std::array<render::water::OceanFFTConfig, 3>, std::array<bool, 3>> {
                auto cfgData = oceanRenderProvider->getOceanFFTConfig();
                float gravity = cfgData.gravity > 0.0f ? cfgData.gravity : oceanRenderProvider->getPhysicsGravity();

                std::array<render::water::OceanFFTConfig, 3> configs;
                std::array<bool, 3> enabled;

                for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
                {
                    const auto& band = cfgData.bands[i];
                    configs[i].resolution = band.resolution;
                    configs[i].patchSize = band.patchSize;
                    configs[i].windSpeed = band.windSpeed;
                    configs[i].windDirection = band.windDirection;
                    configs[i].amplitude = band.amplitude;
                    configs[i].choppiness = band.choppiness;
                    configs[i].gravity = gravity;
                    configs[i].foamThreshold = band.foamThreshold;
                    configs[i].displacementScale = band.displacementScale;
                    enabled[i] = band.enabled;
                }
                return {configs, enabled};
            };

            if (wantOcean && !oceanFFTInitialized)
            {
                auto [configs, enabled] = buildOceanConfigs();
                gpuDrivenRenderer->initOceanFFT(configs, enabled);
                oceanFFTInitialized = true;
                lastOceanConfigVersion = version;

                auto* renderer = gpuDrivenRenderer.get();
                oceanRenderProvider->setOceanHeightSampler(
                    [renderer](const glm::vec2& pos) { return renderer->getOceanHeightAt(pos); });
            }
            else if (!wantOcean && oceanFFTInitialized)
            {
                gpuDrivenRenderer->cleanupOceanFFT();
                oceanFFTInitialized = false;
                oceanRenderProvider->setOceanHeightSampler(nullptr);
            }
            else if (wantOcean && oceanFFTInitialized && version != lastOceanConfigVersion)
            {
                auto [configs, enabled] = buildOceanConfigs();
                gpuDrivenRenderer->updateOceanConfig(configs, enabled);
                lastOceanConfigVersion = version;
            }
        }

        additionalTerrainFrustums.clear();
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
        if (oceanRenderProvider && oceanRenderProvider->hasActiveOcean())
        {
            float waterH = oceanRenderProvider->getBaseWaterHeight();
            float diff = waterH - currentCameraPosition.y;
            camInfo.submersionFactor = glm::clamp((diff + 0.5f) / 1.0f, 0.0f, 1.0f);
            camInfo.isUnderwater = camInfo.submersionFactor > 0.01f;
            camInfo.waterHeight = waterH;
        }
        postProcessPipeline->setCameraData(camInfo);

        if (gpuDrivenRendererInitialized)
            updateSunScreenPosition();

        postProcessPipeline->execute(commandBuffer, imageIndex);
    }

    void RenderPassHandler::executeUpscale(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        auto* upscaleManager = device.getUpscaleManager();
        if (!upscaleManager || !upscaleManager->isActive())
            return;

        // Lazy-create upscale resources if needed
        if (!offscreenResources.upscaleResourcesCreated)
        {
            auto extent = swapChain.getSwapchainExtent();

            // Motion vector image (R16G16_SFLOAT)
            core::ImageInfoRequest mvInfo(device.getLogicalDevice(), device.getPhysicalDevice(),
                extent.width, extent.height, 1, 1,
                vk::Format::eR16G16Sfloat, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            core::ImageUtilities::createImage(mvInfo,
                offscreenResources.motionVectors.image,
                offscreenResources.motionVectors.allocation,
                device.getMemoryManager());

            core::ImageViewInfoRequest mvStorageView(device.getLogicalDevice(),
                offscreenResources.motionVectors.image,
                vk::Format::eR16G16Sfloat, vk::ImageAspectFlagBits::eColor,
                vk::ImageViewType::e2D, 1, 1);
            core::ImageUtilities::createImageView(mvStorageView, offscreenResources.motionVectors.imageView);

            core::ImageViewInfoRequest mvSampledView(device.getLogicalDevice(),
                offscreenResources.motionVectors.image,
                vk::Format::eR16G16Sfloat, vk::ImageAspectFlagBits::eColor,
                vk::ImageViewType::e2D, 1, 1);
            core::ImageUtilities::createImageView(mvSampledView, offscreenResources.motionVectors.sampledView);

            // Upscale output image at display resolution (R16G16B16A16_SFLOAT — SRGB doesn't support storage)
            auto displayExtent = swapChain.getDisplayExtent();
            core::ImageInfoRequest outputInfo(device.getLogicalDevice(), device.getPhysicalDevice(),
                displayExtent.width, displayExtent.height, 1, 1,
                vk::Format::eR16G16B16A16Sfloat, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            core::ImageUtilities::createImage(outputInfo,
                offscreenResources.upscaleOutput.image,
                offscreenResources.upscaleOutput.allocation,
                device.getMemoryManager());

            core::ImageViewInfoRequest outputView(device.getLogicalDevice(),
                offscreenResources.upscaleOutput.image,
                vk::Format::eR16G16B16A16Sfloat, vk::ImageAspectFlagBits::eColor,
                vk::ImageViewType::e2D, 1, 1);
            core::ImageUtilities::createImageView(outputView, offscreenResources.upscaleOutput.imageView);

            offscreenResources.upscaleResourcesCreated = true;
            vfLogInfo("Upscale resources created: MV {}x{}, output {}x{}", extent.width, extent.height, displayExtent.width, displayExtent.height);
        }

        if (!offscreenResources.upscaleResourcesCreated)
            return;

        // Lazy-init motion vector pass
        if (!motionVectorPass)
        {
            motionVectorPass = std::make_unique<upscaling::MotionVectorPass>(device);
            motionVectorPass->init();
        }

        auto& resMgr = upscaleManager->getResolutionManager();
        auto renderRes = resMgr.getRenderResolution();
        auto displayRes = resMgr.getDisplayResolution();

        // Compute inverse VP and prev VP for motion vector generation
        glm::mat4 viewProjection = currentProjection * currentView;
        glm::mat4 invVP = glm::inverse(viewProjection);
        glm::mat4 prevVP = prevProjection * prevView;

        vk::ImageAspectFlags depthStencilAspect =
            vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;

        // Transition depth to shader read for the motion vector compute pass
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            depthStencilAspect);

        // Dispatch motion vector compute pass
        motionVectorPass->dispatch(commandBuffer,
            offscreenResources.depthImage.depthImageView,
            offscreenResources.depthImage.depthImage,
            offscreenResources.motionVectors.imageView,
            offscreenResources.motionVectors.image,
            invVP, prevVP,
            renderRes.width, renderRes.height,
            taaFrameIndex);

        // Transition upscale output to general for write
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.upscaleOutput.image,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
            vk::ImageAspectFlagBits::eColor);

        // Camera cut detection: reset accumulation on large camera jumps or first frame
        bool resetAccum = upscaleFirstFrame;
        if (!upscaleFirstFrame)
        {
            glm::vec3 prevPos = glm::vec3(glm::inverse(prevView)[3]);
            float dist = glm::length(currentCameraPosition - prevPos);
            if (dist > 10.0f) // threshold for teleport detection
                resetAccum = true;
        }
        upscaleFirstFrame = false;

        // Build upscale inputs
        vk::Image colorImage = offscreenResources.colorImages[imageIndex].colorImage;

        render::upscaling::UpscaleInputs inputs{};
        inputs.colorInput = colorImage;
        inputs.colorView = offscreenResources.colorImages[imageIndex].colorImageView;
        inputs.depthInput = offscreenResources.depthImage.depthImage;
        inputs.depthView = offscreenResources.depthImage.depthImageView;
        inputs.motionVectors = offscreenResources.motionVectors.image;
        inputs.motionView = offscreenResources.motionVectors.sampledView;
        inputs.output = offscreenResources.upscaleOutput.image;
        inputs.outputView = offscreenResources.upscaleOutput.imageView;
        inputs.renderExtent = renderRes;
        inputs.displayExtent = displayRes;
        inputs.jitterOffset = currentJitterOffset;
        inputs.resetAccumulation = resetAccum;

        // Try DLSS evaluate; fall back to bilinear blit if it fails
        bool evaluateOk = upscaleManager->evaluate(commandBuffer, taaFrameIndex, inputs);
        if (!evaluateOk)
        {
            // Fallback: blit scene color (render-res) to upscale output (display-res)
            core::ImageUtilities::transitionImageLayout(commandBuffer, colorImage,
                vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
                vk::ImageAspectFlagBits::eColor);

            vk::ImageBlit sceneBlit{};
            sceneBlit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            sceneBlit.srcSubresource.layerCount = 1;
            sceneBlit.srcOffsets[1] = vk::Offset3D{static_cast<int32_t>(renderRes.width),
                                                    static_cast<int32_t>(renderRes.height), 1};
            sceneBlit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            sceneBlit.dstSubresource.layerCount = 1;
            sceneBlit.dstOffsets[1] = vk::Offset3D{static_cast<int32_t>(displayRes.width),
                                                    static_cast<int32_t>(displayRes.height), 1};

            commandBuffer.blitImage(colorImage, vk::ImageLayout::eTransferSrcOptimal,
                                    offscreenResources.upscaleOutput.image, vk::ImageLayout::eGeneral,
                                    sceneBlit, vk::Filter::eLinear);

            core::ImageUtilities::transitionImageLayout(commandBuffer, colorImage,
                vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
        }

        // Transition depth back to attachment optimal
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            depthStencilAspect);
    }

    void RenderPassHandler::executePreUpscalePostProcess(const vk::CommandBuffer& commandBuffer,
                                                          uint32_t imageIndex) const
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
        if (oceanRenderProvider && oceanRenderProvider->hasActiveOcean())
        {
            float waterH = oceanRenderProvider->getBaseWaterHeight();
            float diff = waterH - currentCameraPosition.y;
            camInfo.submersionFactor = glm::clamp((diff + 0.5f) / 1.0f, 0.0f, 1.0f);
            camInfo.isUnderwater = camInfo.submersionFactor > 0.01f;
            camInfo.waterHeight = waterH;
        }
        postProcessPipeline->setCameraData(camInfo);

        if (gpuDrivenRendererInitialized)
            updateSunScreenPosition();

        // Skip TAA when upscaling (DLSS replaces temporal reconstruction)
        postProcessPipeline->executePreUpscale(commandBuffer, imageIndex, true);
    }

    void RenderPassHandler::executePostUpscalePostProcess(const vk::CommandBuffer& commandBuffer,
                                                           uint32_t imageIndex) const
    {
        // Set camera data for post-process effects (tone mapping needs exposure, etc.)
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
        if (oceanRenderProvider && oceanRenderProvider->hasActiveOcean())
        {
            float waterH = oceanRenderProvider->getBaseWaterHeight();
            float diff = waterH - currentCameraPosition.y;
            camInfo.submersionFactor = glm::clamp((diff + 0.5f) / 1.0f, 0.0f, 1.0f);
            camInfo.isUnderwater = camInfo.submersionFactor > 0.01f;
            camInfo.waterHeight = waterH;
        }
        postProcessPipeline->setCameraData(camInfo);

        if (gpuDrivenRendererInitialized)
            updateSunScreenPosition();

        postProcessPipeline->executePostUpscale(commandBuffer, imageIndex,
            offscreenResources.upscaleOutput.image,
            offscreenResources.upscaleOutput.imageView);
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
                                               currentNearPlane, currentFarPlane,
                                               currentTime);
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
