#include "RenderPassHandler.hpp"
#include "print/Log.hpp"
#include "decal/DecalPipeline.hpp"
#include "../core/SwapChain.hpp"
#include "../core/Device.hpp"
#include "../core/ThreadCommandPoolManager.hpp"
#include "../core/RenderManager.hpp"
#include "ClearColor.hpp"
#include "IBL.hpp"
#include "DebugRenderer.hpp"
#include "mesh/StaticMeshPipeline.hpp"
#include "mesh/MeshTypes.hpp"
#include "billboard/BillboardPipeline.hpp"
#include "text/TextPipeline.hpp"
#include "ui/UIRenderPipeline.hpp"
#include <unordered_set>
#include "ui/UITextPipeline.hpp"
#include "occlusion/CameraOcclusionManager.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "gpudriven/terrain/TerrainRaycastPipeline.hpp"
#include "postprocess/PostProcessPipeline.hpp"
#include "volumetric/VolumetricFogComposite.hpp"
#include "gi/SSGIPipeline.hpp"
#include "atmosphere/AtmospherePipeline.hpp"
#include "cloud/CloudPipeline.hpp"
#include "transparency/WBOITPipeline.hpp"
#include "../../services/providers/vfx/IVFXRuntimeProvider.hpp"
#include "../../services/providers/terrain/ITerrainRenderProvider.hpp"
#include "../../services/providers/terrain/IWaterRenderProvider.hpp"
#include "../../services/providers/vegetation/IGrassRenderProvider.hpp"
#include "../../services/data/WaterData.hpp"
#include "water/WaterTypes.hpp"
#include "water/WaterTile.hpp"
#include "water/OceanFFT.hpp"
#include "vegetation/WindConfig.hpp"
#include "threading/JobSystem.hpp"
#include <chrono>

namespace
{
    void recordVFXAfterMeshPass(const vk::CommandBuffer& commandBuffer,
                                render::mesh::StaticMeshPipeline* meshPipeline,
                                services::IVFXRuntimeProvider* vfxProvider, uint32_t imageIndex)
    {
        // Begin mesh pass to clear depth, then immediately end it
        meshPipeline->beginRenderPass(commandBuffer, imageIndex);
        meshPipeline->endRenderPass(commandBuffer);

        // VFX render pass transitions depth to read-only for soft particle sampling
        meshPipeline->beginVFXRenderPass(commandBuffer, imageIndex);
        vfxProvider->recordDrawCommands(commandBuffer);
        meshPipeline->endRenderPass(commandBuffer);
    }
}

namespace render
{
    void RenderPassHandler::draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        clearColor->recordCommandBuffer(commandBuffer, imageIndex);

        if (atmospherePipeline && atmospherePipeline->isEnabled())
        {
            atmospherePipeline->setCameraData(currentView, currentProjection,
                                               currentCameraPosition,
                                               currentNearPlane, currentFarPlane);

            // Feed sun direction from directional light if available
            if (gpuDrivenRendererInitialized)
            {
                auto* lbm = gpuDrivenRenderer->getLightBufferManager();
                auto sunDir = lbm->getFirstDirectionalLightDirection();
                if (sunDir)
                {
                    atmospherePipeline->setSunDirection(*sunDir);
                }
            }

            // Compute dispatch runs on async compute queue when active
            if (!asyncComputeActive)
            {
                atmospherePipeline->dispatchCompute(commandBuffer);
            }
            atmospherePipeline->renderSky(commandBuffer, imageIndex);
        }
        else
        {
            iblRenderer->recordCommandBuffer(commandBuffer, imageIndex);
        }

        // Cloud compute (raymarch + temporal reprojection)
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
                {
                    cloudPipeline->setSunDirection(*sunDir);
                }
            }

            // Feed sun color from atmosphere settings
            if (atmospherePipeline && atmospherePipeline->isInitialized())
            {
                auto atmosSettings = atmospherePipeline->getSettings();
                cloudPipeline->setSunIrradiance(atmosSettings.sunIrradiance);
            }

            // Compute dispatch runs on async compute queue when active
            if (!asyncComputeActive)
            {
                cloudPipeline->dispatchCompute(commandBuffer);
            }
            cloudPipeline->renderComposite(commandBuffer, imageIndex);
        }

        drawSceneMeshes(commandBuffer, imageIndex);

        executeRenderHooks(plugin::RenderPassHookPoint::PostScene, commandBuffer, imageIndex);

        drawOverlays(commandBuffer, imageIndex);
        executeOcclusionPasses(commandBuffer);

        if (atmospherePipeline && atmospherePipeline->isEnabled())
        {
            atmospherePipeline->renderComposite(commandBuffer, imageIndex);
        }

        if (volumetricFogComposite && volumetricFogComposite->isInitialized())
        {
            volumetricFogComposite->setCameraData(currentNearPlane, currentFarPlane);
            volumetricFogComposite->execute(commandBuffer, imageIndex);
        }

        if (ssgiPipeline && ssgiPipeline->isInitialized())
        {
            ssgiPipeline->setCameraData(currentView, currentProjection,
                                         currentCameraPosition,
                                         currentNearPlane, currentFarPlane,
                                         taaFrameIndex);
            ssgiPipeline->execute(commandBuffer, imageIndex);
        }

        executeRenderHooks(plugin::RenderPassHookPoint::PrePostProcess, commandBuffer, imageIndex);

        executePostProcess(commandBuffer, imageIndex);

        executeRenderHooks(plugin::RenderPassHookPoint::PostPostProcess, commandBuffer, imageIndex);

        drawUIOverlays(commandBuffer, imageIndex);

        executeRenderHooks(plugin::RenderPassHookPoint::Overlay, commandBuffer, imageIndex);
    }

    void RenderPassHandler::updateGPUDrivenSceneData() const
    {
        if (!gpuDrivenRendererInitialized || !meshPipelineInitialized)
        {
            return;
        }

        gpuDrivenRenderer->updateScene(
            currentMeshDrawList, currentView, currentProjection,
            currentCameraPosition, currentNearPlane, currentFarPlane, currentTime);

        if (terrainRenderProvider && terrainRenderProvider->hasActiveTerrain() && currentFrustum)
        {
            if (terrainRenderProvider->consumeTerrainMaterialDirty())
            {
                gpuDrivenRenderer->invalidateTerrainLayerData();
            }

            auto visibleTiles = terrainRenderProvider->getVisibleTiles(*currentFrustum, currentCameraPosition);

            // Merge terrain tiles visible to additional cameras (RTT) so they are
            // available on the GPU for RTT render passes in the next frame.
            // Uses queryVisibleTiles (frustum+distance only) to avoid side effects
            // like LOD changes and isVisible flag corruption on the main camera tiles.
            std::unordered_set<::terrain::TerrainTile*> terrainSeen(visibleTiles.begin(), visibleTiles.end());
            for (const auto& [rttFrustum, rttCameraPos] : additionalTerrainFrustums)
            {
                auto rttTiles = terrainRenderProvider->queryVisibleTiles(rttFrustum, rttCameraPos);
                for (auto* tile : rttTiles)
                {
                    if (terrainSeen.insert(tile).second)
                    {
                        visibleTiles.push_back(tile);
                    }
                }
            }

            auto matPath = terrainRenderProvider->getTerrainMaterialPath();
            gpuDrivenRenderer->updateTerrain(visibleTiles, currentCameraPosition, matPath);

            auto allLoadedTiles = terrainRenderProvider->getAllLoadedTiles();
            gpuDrivenRenderer->updateVegetationStreaming(visibleTiles, allLoadedTiles, currentCameraPosition);

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
                ::vegetation::WindConfig windConfig;
                gpuDrivenRenderer->updateWind(0.016f, windConfig);
            }
        }

        if (waterRenderProvider && waterRenderProvider->hasActiveWater() && currentFrustum)
        {
            auto visibleTiles = waterRenderProvider->getVisibleWaterTiles(*currentFrustum, currentCameraPosition);

            // Merge water tiles visible to additional cameras (RTT) so they are
            // available on the GPU for RTT render passes in the next frame.
            // Uses queryVisibleWaterTiles to avoid corrupting isVisible flags.
            std::unordered_set<::water::WaterTile*> waterSeen(visibleTiles.begin(), visibleTiles.end());
            for (const auto& [rttFrustum, rttCameraPos] : additionalWaterFrustums)
            {
                auto rttTiles = waterRenderProvider->queryVisibleWaterTiles(rttFrustum, rttCameraPos);
                for (auto* tile : rttTiles)
                {
                    if (waterSeen.insert(tile).second)
                    {
                        visibleTiles.push_back(tile);
                    }
                }
            }

            auto settings = waterRenderProvider->getWaterGlobalSettings();
            auto tileConfig = waterRenderProvider->getWaterTileConfig();
            gpuDrivenRenderer->updateWater(visibleTiles, settings, tileConfig);
        }

        if (waterRenderProvider && gpuDrivenRenderer)
        {
            bool wantOcean = waterRenderProvider->isOceanFFTEnabled();
            uint32_t version = waterRenderProvider->getOceanFFTConfigVersion();

            if (wantOcean && !oceanFFTInitialized)
            {
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
                gpuDrivenRenderer->initOceanFFT(cfg);
                oceanFFTInitialized = true;
                lastOceanConfigVersion = version;

                // Wire CPU-side ocean height sampling for physics
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
                gpuDrivenRenderer->updateOceanConfig(cfg);
                lastOceanConfigVersion = version;
            }
        }

        // Consume and discard RTT frustums so they don't persist across frames.
        // renderAll() re-populates them each frame during play mode.
        additionalTerrainFrustums.clear();
        additionalWaterFrustums.clear();
    }

    void RenderPassHandler::drawSceneMeshes(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        bool hasDebugItems = debugRendererInitialized && debugRenderer->hasItemsToRender();
        bool hasVFX = vfxRuntimeProvider && vfxRuntimeProvider->isInitialized()
            && vfxRuntimeProvider->getInstanceCount() > 0;
        bool hasCustomShaderMeshes = !customShaderMeshDrawList.empty();

        if (hasVFX)
        {
            services::VFXCameraParams vfxCamera;
            vfxCamera.view = currentView;
            vfxCamera.projection = currentProjection;
            vfxCamera.cameraPos = currentCameraPosition;
            vfxCamera.time = currentTime;
            vfxCamera.nearPlane = currentNearPlane;
            vfxCamera.farPlane = currentFarPlane;
            vfxRuntimeProvider->setCamera(vfxCamera);
            vfxRuntimeProvider->setSceneDepthImageView(offscreenResources.depthImage.depthImageView);

            // Update per-frame lighting descriptor sets for VFX.
            // Lighting layouts are set once during init (RenderPassHandler::initPipelines),
            // before VFX pipeline creation. Only descriptor sets need per-frame updates.
            if (vfxLightingInitialized && gpuDrivenRendererInitialized && gpuDrivenRenderer)
            {
                auto* lbm = gpuDrivenRenderer->getLightBufferManager();
                auto* cgm = gpuDrivenRenderer->getClusterGridManager();
                auto* lcp = gpuDrivenRenderer->getLightCullingPipeline();

                if (lbm && cgm && lcp)
                {
                    vfxRuntimeProvider->updateLightingDescriptorSets(
                        lbm->getDescriptorSet(),
                        cgm->getDescriptorSet(),
                        lcp->getDescriptorSet());
                }
            }
        }

        bool hasTerrainToRender = gpuDrivenRenderer && gpuDrivenRenderer->isTerrainRenderingEnabled()
            && terrainRenderProvider && terrainRenderProvider->hasActiveTerrain();

        bool hasWaterToRender = gpuDrivenRenderer && gpuDrivenRenderer->isWaterRenderingEnabled()
            && waterRenderProvider && waterRenderProvider->hasActiveWater();

        bool needsMeshPass = meshPipelineInitialized && (!currentMeshDrawList.empty() || hasCustomShaderMeshes
            || hasDebugItems || hasVFX || hasTerrainToRender || hasWaterToRender);

        updateGPUDrivenSceneData();

        if (!needsMeshPass)
        {
            // Still render decals even without meshes (they project onto previously rendered geometry)
            if (decalRenderingEnabled && decalPipeline && decalPipeline->isInitialized() && decalPipeline->hasDecals())
            {
                decalPipeline->setCameraData(currentView, currentProjection, currentNearPlane, currentFarPlane);
                decalPipeline->render(commandBuffer, imageIndex);
            }
            return;
        }

        DebugRenderer* debugRendererPtr = hasDebugItems ? debugRenderer.get() : nullptr;

        // VFX compute runs on async compute queue when active
        if (hasVFX && !asyncComputeActive)
        {
            vfxRuntimeProvider->recordComputeCommands(commandBuffer);
        }

        bool hasMeshesToRender = !currentMeshDrawList.empty();

        if ((hasMeshesToRender || hasTerrainToRender || hasWaterToRender) && gpuDrivenRendererInitialized && gpuDrivenRenderer->isEnabled())
        {
            drawGPUDrivenMeshPass(commandBuffer, imageIndex, debugRendererPtr, hasCustomShaderMeshes, hasVFX);
        }
        else if (!currentMeshDrawList.empty() || hasCustomShaderMeshes || hasDebugItems)
        {
            meshPipeline->recordCommandBuffer(commandBuffer, imageIndex, combinedMeshDrawList, currentFrustum,
                                              debugRendererPtr, currentView, currentProjection);

            if (hasVFX)
            {
                recordVFXAfterMeshPass(commandBuffer, meshPipeline.get(), vfxRuntimeProvider, imageIndex);
            }
        }
        else if (hasVFX)
        {
            recordVFXAfterMeshPass(commandBuffer, meshPipeline.get(), vfxRuntimeProvider, imageIndex);
        }
    }

    void RenderPassHandler::drawGPUDrivenMeshPass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                                   DebugRenderer* debugRendererPtr, bool hasCustomShaderMeshes,
                                                   bool hasVFX) const
    {
        updateGPUDrivenHiZ();
        if (asyncComputeActive)
        {
            // Async compute path: light culling, grass, GI run on async compute queue
            // Graphics queue handles uploads, object culling, shadows, volumetric fog
            gpuDrivenRenderer->dispatchGraphicsCompute(commandBuffer);
        }
        else
        {
            // Single-queue fallback: all compute on graphics queue
            gpuDrivenRenderer->dispatchCompute(commandBuffer);
        }

        if (oceanFFTInitialized)
        {
            // Read previous frame's displacement data for CPU-side physics
            gpuDrivenRenderer->readbackOceanDisplacement();
            // Ocean FFT compute runs on async compute queue when active
            if (!asyncComputeActive)
            {
                gpuDrivenRenderer->dispatchOceanFFT(commandBuffer, currentTime);
            }
        }

        vk::DescriptorSet iblDescriptorSet = meshPipeline->getIBLDescriptorSet(imageIndex);

        bool useParallel = parallelSceneRecording && sceneThreadPoolManager &&
                           sceneThreadPoolManager->getThreadCount() > 1;

        if (useParallel)
        {
            // Parallel path: record independent draw groups on worker threads
            auto sceneRecordStart = std::chrono::high_resolution_clock::now();
            uint32_t frameIndex = core::RenderManager::getImageIndex();
            sceneThreadPoolManager->resetFrame(frameIndex);

            vk::RenderPass rp = meshPipeline->getRenderPass();
            vk::Framebuffer fb = meshPipeline->getFramebuffer(imageIndex);
            auto extent = swapChain.getSwapchainExtent();

            auto setupSecondary = [&](vk::CommandBuffer sec) {
                vk::CommandBufferInheritanceInfo inheritance{};
                inheritance.renderPass = rp;
                inheritance.subpass = 0;
                inheritance.framebuffer = fb;

                vk::CommandBufferBeginInfo beginInfo{};
                beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit |
                                  vk::CommandBufferUsageFlagBits::eRenderPassContinue;
                beginInfo.pInheritanceInfo = &inheritance;
                sec.begin(beginInfo);

                vk::Viewport viewport{0.0f, 0.0f,
                    static_cast<float>(extent.width), static_cast<float>(extent.height),
                    0.0f, 1.0f};
                sec.setViewport(0, viewport);
                vk::Rect2D scissor{{0, 0}, extent};
                sec.setScissor(0, scissor);
            };

            bool useWBOIT = wboitEnabled && wboitPipeline && wboitPipeline->isInitialized()
                            && gpuDrivenRenderer->isWBOITReady();
            bool hasTerrain = gpuDrivenRenderer->isTerrainRenderingEnabled();
            bool hasGrass = gpuDrivenRenderer->isGrassRenderingEnabled();
            bool hasWater = gpuDrivenRenderer->isWaterRenderingEnabled();
            bool hasBillboards = gpuDrivenRenderer->isBillboardRenderingEnabled();

            // Determine how many secondary buffers we need
            // Thread 0: opaque + transparent + blend (main mesh draws)
            // Thread 1: terrain (if enabled)
            // Thread 2: grass (if enabled)
            // Thread 3: water + billboards (if enabled)
            struct SecondaryTask {
                uint32_t threadIdx;
                vk::CommandBuffer cmd{nullptr};
                bool used = false;
            };

            SecondaryTask meshTask{0};
            SecondaryTask terrainTask{1};
            SecondaryTask grassTask{2};
            SecondaryTask waterTask{3};

            // Submit parallel jobs for independent draw groups
            auto meshFuture = threading::JobSystem::instance().submit([&]() {
                meshTask.cmd = sceneThreadPoolManager->getSecondary(0, frameIndex);
                setupSecondary(meshTask.cmd);
                gpuDrivenRenderer->renderDraw(meshTask.cmd, iblDescriptorSet);
                if (!useWBOIT) gpuDrivenRenderer->renderTransparentDraw(meshTask.cmd, iblDescriptorSet);
                gpuDrivenRenderer->renderBlendDraw(meshTask.cmd, iblDescriptorSet);
                meshTask.cmd.end();
                meshTask.used = true;
            }, threading::JobPriority::HIGH);

            std::future<void> terrainFuture;
            if (hasTerrain)
            {
                terrainFuture = threading::JobSystem::instance().submit([&]() {
                    terrainTask.cmd = sceneThreadPoolManager->getSecondary(1, frameIndex);
                    setupSecondary(terrainTask.cmd);
                    gpuDrivenRenderer->renderTerrainDraw(terrainTask.cmd, iblDescriptorSet);
                    terrainTask.cmd.end();
                    terrainTask.used = true;
                }, threading::JobPriority::HIGH);
            }

            std::future<void> grassFuture;
            if (hasGrass)
            {
                grassFuture = threading::JobSystem::instance().submit([&]() {
                    grassTask.cmd = sceneThreadPoolManager->getSecondary(2, frameIndex);
                    setupSecondary(grassTask.cmd);
                    gpuDrivenRenderer->renderGrassDraw(grassTask.cmd, iblDescriptorSet);
                    grassTask.cmd.end();
                    grassTask.used = true;
                }, threading::JobPriority::HIGH);
            }

            std::future<void> waterFuture;
            if (hasWater || hasBillboards)
            {
                waterFuture = threading::JobSystem::instance().submit([&]() {
                    waterTask.cmd = sceneThreadPoolManager->getSecondary(3, frameIndex);
                    setupSecondary(waterTask.cmd);
                    if (hasWater) gpuDrivenRenderer->renderWaterDraw(waterTask.cmd, iblDescriptorSet);
                    if (hasBillboards) gpuDrivenRenderer->renderBillboardDraw(waterTask.cmd, iblDescriptorSet);
                    waterTask.cmd.end();
                    waterTask.used = true;
                }, threading::JobPriority::HIGH);
            }

            // Wait for all parallel recordings to complete
            meshFuture.get();
            if (terrainFuture.valid()) terrainFuture.get();
            if (grassFuture.valid()) grassFuture.get();
            if (waterFuture.valid()) waterFuture.get();

            // Begin render pass with secondary buffer support and execute
            meshPipeline->beginRenderPassForSecondary(commandBuffer, imageIndex);

            std::vector<vk::CommandBuffer> secondaries;
            secondaries.reserve(4);
            if (meshTask.used) secondaries.push_back(meshTask.cmd);
            if (terrainTask.used) secondaries.push_back(terrainTask.cmd);
            if (grassTask.used) secondaries.push_back(grassTask.cmd);
            if (waterTask.used) secondaries.push_back(waterTask.cmd);

            if (!secondaries.empty())
            {
                commandBuffer.executeCommands(
                    static_cast<uint32_t>(secondaries.size()),
                    secondaries.data()
                );
            }

            auto sceneRecordEnd = std::chrono::high_resolution_clock::now();
            lastSceneRecordingUs = std::chrono::duration<float, std::micro>(sceneRecordEnd - sceneRecordStart).count();
            lastSceneSecondaryCount = static_cast<uint32_t>(secondaries.size());

            // Custom shaders, GI debug, debug renderer need inline — end secondary pass first
            meshPipeline->endRenderPass(commandBuffer);

            // Record remaining items inline in a new render pass (eLoad to preserve)
            if (hasCustomShaderMeshes || debugRendererPtr || gpuDrivenRenderer)
            {
                bool needsInlinePass = hasCustomShaderMeshes ||
                    (gpuDrivenRenderer && gpuDrivenRenderer->getGISettings().showProbes) ||
                    (debugRendererPtr && debugRendererPtr->hasItemsToRender());

                if (needsInlinePass)
                {
                    meshPipeline->beginRenderPass(commandBuffer, imageIndex);

                    vk::Viewport vp{0.0f, 0.0f,
                        static_cast<float>(extent.width), static_cast<float>(extent.height),
                        0.0f, 1.0f};
                    commandBuffer.setViewport(0, vp);
                    vk::Rect2D sc{{0, 0}, extent};
                    commandBuffer.setScissor(0, sc);

                    if (hasCustomShaderMeshes)
                    {
                        meshPipeline->renderMeshList(commandBuffer, imageIndex, customShaderMeshDrawList, currentFrustum);
                    }
                    gpuDrivenRenderer->renderGIDebug(commandBuffer, currentProjection * currentView);
                    if (debugRendererPtr)
                    {
                        debugRendererPtr->render(commandBuffer, combinedMeshDrawList, currentView, currentProjection,
                            [this](const std::string& meshId) { return meshPipeline->getMesh(meshId); });
                    }

                    meshPipeline->endRenderPass(commandBuffer);
                }
            }
        }
        else
        {
            // Original inline path
            meshPipeline->beginRenderPass(commandBuffer, imageIndex);

            auto extent = swapChain.getSwapchainExtent();
            vk::Viewport viewport{0.0f, 0.0f,
                                   static_cast<float>(extent.width), static_cast<float>(extent.height),
                                   0.0f, 1.0f};
            commandBuffer.setViewport(0, viewport);
            vk::Rect2D scissor{{0, 0}, extent};
            commandBuffer.setScissor(0, scissor);

            gpuDrivenRenderer->renderDraw(commandBuffer, iblDescriptorSet);

            bool useWBOIT = wboitEnabled && wboitPipeline && wboitPipeline->isInitialized()
                            && gpuDrivenRenderer->isWBOITReady();
            if (!useWBOIT)
            {
                gpuDrivenRenderer->renderTransparentDraw(commandBuffer, iblDescriptorSet);
            }

            gpuDrivenRenderer->renderBlendDraw(commandBuffer, iblDescriptorSet);

            if (gpuDrivenRenderer->isTerrainRenderingEnabled())
            {
                gpuDrivenRenderer->renderTerrainDraw(commandBuffer, iblDescriptorSet);
            }

            if (gpuDrivenRenderer->isGrassRenderingEnabled())
            {
                gpuDrivenRenderer->renderGrassDraw(commandBuffer, iblDescriptorSet);
            }

            if (gpuDrivenRenderer->isWaterRenderingEnabled())
            {
                gpuDrivenRenderer->renderWaterDraw(commandBuffer, iblDescriptorSet);
            }

            if (gpuDrivenRenderer->isBillboardRenderingEnabled())
            {
                gpuDrivenRenderer->renderBillboardDraw(commandBuffer, iblDescriptorSet);
            }

            if (hasCustomShaderMeshes)
            {
                meshPipeline->renderMeshList(commandBuffer, imageIndex, customShaderMeshDrawList, currentFrustum);
            }

            if (gpuDrivenRenderer)
            {
                gpuDrivenRenderer->renderGIDebug(commandBuffer, currentProjection * currentView);
            }

            if (debugRendererPtr)
            {
                debugRendererPtr->render(commandBuffer, combinedMeshDrawList, currentView, currentProjection,
                                         [this](const std::string& meshId)
                                         {
                                             return meshPipeline->getMesh(meshId);
                                         });
            }

            meshPipeline->endRenderPass(commandBuffer);
        }

        // Decal pass: project decals onto scene geometry
        if (decalRenderingEnabled && decalPipeline && decalPipeline->isInitialized() && decalPipeline->hasDecals())
        {
            decalPipeline->setCameraData(currentView, currentProjection, currentNearPlane, currentFarPlane);
            decalPipeline->render(commandBuffer, imageIndex);
        }

        bool wboitActive = wboitEnabled && wboitPipeline && wboitPipeline->isInitialized()
                           && gpuDrivenRenderer->isWBOITReady();
        if (wboitActive && gpuDrivenRenderer->hasTransparentObjects())
        {
            wboitPipeline->beginWBOITPass(commandBuffer, imageIndex);
            gpuDrivenRenderer->renderWBOITDraw(commandBuffer, iblDescriptorSet);
            wboitPipeline->endWBOITPass(commandBuffer);
            wboitPipeline->composite(commandBuffer, imageIndex);
        }

        if (hasVFX)
        {
            meshPipeline->beginVFXRenderPass(commandBuffer, imageIndex);
            vfxRuntimeProvider->recordDrawCommands(commandBuffer);
            meshPipeline->endRenderPass(commandBuffer);
        }
    }

    void RenderPassHandler::drawOverlays(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        if (billboardPipelineInitialized && !currentBillboardDrawList.empty())
        {
            billboardPipeline->recordCommandBuffer(commandBuffer, imageIndex);
        }

        if (textPipelineInitialized && !currentTextDrawList.empty())
        {
            textPipeline->recordCommandBuffer(commandBuffer, imageIndex);
        }
    }

    void RenderPassHandler::executeOcclusionPasses(const vk::CommandBuffer& commandBuffer) const
    {
        occlusion::CameraId activeCameraId = cameraOcclusionManager->getActiveCameraId();

        if (!cameraOcclusionManager->isHiZInitialized(activeCameraId))
        {
            return;
        }

        cameraOcclusionManager->generateHiZ(activeCameraId, commandBuffer);

        if (terrainRaycastPipeline && terrainRaycastPipeline->isInitialized())
        {
            dispatchTerrainRaycast(commandBuffer);
        }

        // VSM Feedback pass - dispatch after depth is available
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer->getShadowSystem() &&
            gpuDrivenRenderer->getShadowSystem()->isFeedbackEnabled())
        {
            auto* shadowSystem = gpuDrivenRenderer->getShadowSystem();
            auto extent = swapChain.getSwapchainExtent();
            glm::mat4 invVP = glm::inverse(currentProjection * currentView);

            // Transition depth to shader-read for feedback compute
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

            // Transition depth back to attachment layout
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
        {
            updateSunScreenPosition();
        }

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
        {
            uiPipeline->recordCommandBuffer(commandBuffer, imageIndex);
        }

        if (uiTextPipelineInitialized && !currentUITextDrawList.empty())
        {
            uiTextPipeline->recordCommandBuffer(commandBuffer, imageIndex);
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
        // GPU-driven compute: light culling, grass, GI probes
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer->isEnabled())
        {
            gpuDrivenRenderer->dispatchAsyncCompute(asyncCmd);
        }

        // Atmosphere compute (LUT generation)
        // Camera/sun data must be set before dispatch
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
                {
                    atmospherePipeline->setSunDirection(*sunDir);
                }
            }
            atmospherePipeline->dispatchCompute(asyncCmd);
        }

        // Cloud compute (raymarch + temporal reprojection)
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
                {
                    cloudPipeline->setSunDirection(*sunDir);
                }
            }
            if (atmospherePipeline && atmospherePipeline->isInitialized())
            {
                auto atmosSettings = atmospherePipeline->getSettings();
                cloudPipeline->setSunIrradiance(atmosSettings.sunIrradiance);
            }
            cloudPipeline->dispatchCompute(asyncCmd);
        }

        // VFX/particle compute
        if (vfxRuntimeProvider)
        {
            vfxRuntimeProvider->recordComputeCommands(asyncCmd);
        }

        // Ocean FFT
        if (oceanFFTInitialized && gpuDrivenRendererInitialized)
        {
            gpuDrivenRenderer->dispatchOceanFFT(asyncCmd, currentTime);
        }
    }
}
