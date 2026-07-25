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
#include "upscaling/ReactiveMaskPass.hpp"
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
#include "terrain/TerrainTile.hpp"
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
                // VK-1336: queryVisibleTiles is now non-mutating (preserves the main camera's
                // tile->isVisible flags). Re-mark RTT-only tiles as visible here so they get
                // uploaded for the minimap/RTT pass to render them.
                for (auto* tile : terrainRenderProvider->queryVisibleTiles(rttFrustum, rttCameraPos))
                {
                    if (terrainSeen.insert(tile).second)
                    {
                        tile->isVisible = true;
                        visibleTiles.push_back(tile);
                    }
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

        }

        // VK-1580: update the GLOBAL wind every frame — outside the terrain block — so
        // foliage-mesh sway works even in scenes without active terrain (previously the wind
        // buffer only updated when terrain was present). Config comes from grass when a grass
        // provider exists, else a sensible default so painted foliage still sways.
        if (gpuDrivenRenderer)
        {
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

            // VK-1605: tick the time-sliced shore-depth bake. It has to be driven from here rather
            // than from OceanService::update because this is where the actual view camera position
            // lives - the same one the water tile grid centres on (in the Editor that is the
            // viewport camera, not a scene CameraComponent).
            oceanRenderProvider->updateShoreDepthField(
                glm::vec2(currentCameraPosition.x, currentCameraPosition.z));

            // VK-1606: hand this frame's water impulses to the ripple sim. Drained here rather than
            // in the dispatch itself so the service-side queue is emptied exactly once per frame,
            // whatever the sim then decides to do with them (it may run 0 sub-steps). The dispatch
            // that consumes them is recorded later in the SAME function that calls this one
            // (RenderPassHandlerDraw), so the ordering is structural, not incidental.
            gpuDrivenRenderer->queueWaterImpulses(oceanRenderProvider->drainWaterImpulses());

            auto visualSettings = oceanRenderProvider->getOceanVisualSettings();
            float baseHeight = oceanRenderProvider->getBaseWaterHeight();
            auto cfgData = oceanRenderProvider->getOceanFFTConfig();
            const auto* tileGrid = oceanRenderProvider->getWaterTileGrid();
            const auto* shoreField = oceanRenderProvider->getShoreDepthField();
            bool worldMode = oceanRenderProvider->isWorldModeActive();
            gpuDrivenRenderer->updateWater(visualSettings, baseHeight,
                                            currentCameraPosition, cfgData.bands[0].patchSize,
                                            worldMode, tileGrid, shoreField);
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
                    configs[i].foamPersistence = band.foamPersistence;
                    configs[i].foamDecay = band.foamDecay;
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
        // VK-1336: main-scene HiZ generation and occlusion always target the primary camera.
        // RTT cameras run their own cull/HiZ inside RenderTextureViewPort's per-RTT context.
        if (!cameraOcclusionManager->isHiZInitialized(occlusion::MAIN_CAMERA_ID))
            return;

        cameraOcclusionManager->generateHiZ(occlusion::MAIN_CAMERA_ID, commandBuffer);

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

    void RenderPassHandler::executeUpscaleGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        auto* upscaleManager = device.getUpscaleManager();
        if (!upscaleManager || !upscaleManager->isActive())
            return;

        if (!offscreenResources.upscaleResourcesCreated)
            return;

        if (!motionVectorPass)
        {
            motionVectorPass = std::make_unique<upscaling::MotionVectorPass>(device);
            motionVectorPass->init();
        }

        auto& resMgr = upscaleManager->getResolutionManager();
        auto renderRes = resMgr.getRenderResolution();
        auto displayRes = resMgr.getDisplayResolution();

        // Depth transition handled by render graph (already in DepthStencilReadOnlyOptimal)

        if (asyncComputeActive && offscreenResources.prevFrameDepthCreated
            && motionVectorPass && motionVectorPass->isInitialized())
        {
            // Motion vectors already computed on async compute queue — ready for upscaler
        }
        else
        {
            // Fallback: dispatch motion vectors on graphics queue
            glm::mat4 viewProjection = currentProjection * currentView;
            glm::mat4 invVP = glm::inverse(viewProjection);
            glm::mat4 prevVP = prevProjection * prevView;

            // Depth already in DepthStencilReadOnlyOptimal via graph

            motionVectorPass->dispatch(commandBuffer,
                offscreenResources.depthImage.depthImageView,
                offscreenResources.depthImage.depthImage,
                offscreenResources.motionVectors.imageView,
                offscreenResources.motionVectors.image,
                invVP, prevVP,
                renderRes.width, renderRes.height,
                taaFrameIndex);
        }

        // Transition upscale output to general for write (internal resource)
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.upscaleOutput.image,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
            vk::ImageAspectFlagBits::eColor);

        bool resetAccum = upscaleFirstFrame;
        if (!upscaleFirstFrame)
        {
            glm::vec3 prevPos = glm::vec3(glm::inverse(prevView)[3]);
            float dist = glm::length(currentCameraPosition - prevPos);
            if (dist > 10.0f)
                resetAccum = true;
        }
        upscaleFirstFrame = false;

        // Write auto-exposure value to the 1x1 exposure texture for Streamline
        float exposureValue = 1.0f;
        if (postProcessPipeline)
        {
            auto exp = postProcessPipeline->getComputedExposure();
            if (exp.has_value())
                exposureValue = exp.value();
        }

        if (offscreenResources.exposureImage.image)
        {
            core::ImageUtilities::transitionImageLayout(commandBuffer,
                offscreenResources.exposureImage.image,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageAspectFlagBits::eColor);

            vk::ClearColorValue clearValue;
            clearValue.setFloat32({exposureValue, 0.0f, 0.0f, 0.0f});
            vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            commandBuffer.clearColorImage(offscreenResources.exposureImage.image,
                vk::ImageLayout::eTransferDstOptimal, clearValue, range);

            core::ImageUtilities::transitionImageLayout(commandBuffer,
                offscreenResources.exposureImage.image,
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
        }

        vk::Image colorImage = offscreenResources.colorImages[imageIndex].colorImage;

        // Generate the reactive/transparency mask from the opaque-only color
        // snapshot taken before the VFX/WBOIT passes (scene color is in
        // ShaderReadOnlyOptimal here, sampled directly)
        bool reactiveMaskReady = false;
        if (preTransparencyCaptured && offscreenResources.reactiveMask.image)
        {
            if (!reactiveMaskPass)
            {
                reactiveMaskPass = std::make_unique<upscaling::ReactiveMaskPass>(device);
                reactiveMaskPass->init();
            }
            if (reactiveMaskPass->isInitialized())
            {
                reactiveMaskPass->dispatch(commandBuffer,
                    offscreenResources.preTransparencyColor.imageView,
                    offscreenResources.colorImages[imageIndex].colorImageView,
                    offscreenResources.reactiveMask.imageView,
                    offscreenResources.reactiveMask.image,
                    renderRes.width, renderRes.height,
                    taaFrameIndex);
                reactiveMaskReady = true;
            }
        }
        preTransparencyCaptured = false;

        render::upscaling::UpscaleInputs inputs{};
        inputs.colorInput = colorImage;
        inputs.colorView = offscreenResources.colorImages[imageIndex].colorImageView;
        inputs.depthInput = offscreenResources.depthImage.depthImage;
        inputs.depthView = offscreenResources.depthImage.depthImageView;
        inputs.motionVectors = offscreenResources.motionVectors.image;
        inputs.motionView = offscreenResources.motionVectors.sampledView;
        if (reactiveMaskReady)
        {
            inputs.reactiveMask = offscreenResources.reactiveMask.image;
            inputs.reactiveView = offscreenResources.reactiveMask.imageView;
        }
        inputs.exposureImage = offscreenResources.exposureImage.image;
        inputs.exposureView = offscreenResources.exposureImage.imageView;
        inputs.output = offscreenResources.upscaleOutput.image;
        inputs.outputView = offscreenResources.upscaleOutput.imageView;
        inputs.renderExtent = renderRes;
        inputs.displayExtent = displayRes;
        inputs.jitterOffset = currentJitterOffset;
        inputs.preExposure = exposureValue;
        inputs.resetAccumulation = resetAccum;
        inputs.viewMatrix = currentView;
        inputs.projectionMatrix = currentProjection;
        inputs.prevViewMatrix = prevView;
        inputs.prevProjectionMatrix = prevProjection;
        inputs.cameraPosition = currentCameraPosition;
        inputs.nearPlane = currentNearPlane;
        inputs.farPlane = currentFarPlane;

        // DLSS-D Ray Reconstruction normal-roughness guide: the depth-prepass normal target
        // already packs roughness in .w (NORMAL_FORMAT = R16G16B16A16_SFLOAT), matching
        // DLSSDNormalRoughnessMode::ePacked. Only fed when RR is the active upscaler. (VK-1245)
        if (upscaleManager->isDLSSRRActive() && gpuDrivenRenderer)
        {
            inputs.normalRoughness = gpuDrivenRenderer->getPrepassNormalImage();
            inputs.normalRoughnessView = gpuDrivenRenderer->getPrepassNormalImageView();

            // VK-1397: depth-prepass albedo demodulation guides. Null-guarded by the
            // tagger, and produced only when the prepass runs (occlusion culling on).
            // The prepass leaves these in SHADER_READ_ONLY_OPTIMAL ready for the tag.
            inputs.diffuseAlbedo = gpuDrivenRenderer->getPrepassDiffuseAlbedoImage();
            inputs.diffuseAlbedoView = gpuDrivenRenderer->getPrepassDiffuseAlbedoImageView();
            inputs.specularAlbedo = gpuDrivenRenderer->getPrepassSpecularAlbedoImage();
            inputs.specularAlbedoView = gpuDrivenRenderer->getPrepassSpecularAlbedoImageView();

            // Satisfy RR's specular-MV / hit-distance requirement by reusing the dense
            // motion vectors as specular MV (no RT reflections → specular tracks the
            // surface). Already SHADER_READ for the regular motion-vector tag. (VK-1397)
            inputs.specularMotionVectors = offscreenResources.motionVectors.image;
            inputs.specularMotionVectorsView = offscreenResources.motionVectors.sampledView;
        }

        bool evaluateOk = upscaleManager->evaluate(commandBuffer, taaFrameIndex, inputs);
        if (evaluateOk)
        {
            core::ImageUtilities::transitionImageLayout(commandBuffer,
                offscreenResources.upscaleOutput.image,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                vk::ImageAspectFlagBits::eColor);
        }
        if (!evaluateOk)
        {
            // Fallback blit (internal intermediate transitions on scene color)
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

        // Depth final state handled by render graph — no restoration needed
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

        postProcessPipeline->executePreUpscale(commandBuffer, imageIndex);
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
            ctx.colorFormat      = swapChain.getSceneColorFormat();
            ctx.depthFormat      = swapChain.getSwapchainDepthStencilFormat();
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

    void RenderPassHandler::recordAsyncCompute(vk::CommandBuffer asyncCmd, uint32_t frameIndex) const
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
        {
            // VK-1502: feed the VFX sim camera on the async path too — the sync path feeds it via setCamera
            // in drawSceneMeshesGraphManaged, which does not run when the sim is recorded on the async queue.
            services::VFXCameraParams vfxCamera;
            vfxCamera.view = currentView;
            vfxCamera.projection = currentProjection;
            vfxCamera.cameraPos = currentCameraPosition;
            vfxCamera.time = currentTime;
            vfxCamera.nearPlane = currentNearPlane;
            vfxCamera.farPlane = currentFarPlane;
            vfxRuntimeProvider->setCamera(vfxCamera);

            vfxRuntimeProvider->recordComputeCommands(asyncCmd);
        }

        // Async compute motion vectors using previous-frame depth
        if (offscreenResources.prevFrameDepthCreated && offscreenResources.upscaleResourcesCreated)
        {
            auto* upscaleManager = device.getUpscaleManager();
            if (upscaleManager && upscaleManager->isActive() && motionVectorPass && motionVectorPass->isInitialized())
            {
                auto& resMgr = upscaleManager->getResolutionManager();
                auto renderRes = resMgr.getRenderResolution();

                // Read depth from the same flight slot (written by frame N-2, guaranteed complete
                // by the in-flight fence). Using current-frame VP matrices with stale depth
                // produces approximate motion vectors — camera motion dominates and DLSS
                // have built-in robustness to handle slight inaccuracy.
                auto& prevDepth = offscreenResources.prevFrameDepth[frameIndex];

                glm::mat4 invVP = glm::inverse(currentProjection * currentView);
                glm::mat4 prevVP = prevProjection * prevView;

                motionVectorPass->dispatch(asyncCmd,
                    prevDepth.imageView,
                    prevDepth.image,
                    offscreenResources.motionVectors.imageView,
                    offscreenResources.motionVectors.image,
                    invVP, prevVP,
                    renderRes.width, renderRes.height,
                    taaFrameIndex,
                    vk::ImageLayout::eShaderReadOnlyOptimal);
            }
        }
    }
}
