#include "GPUDrivenRenderer.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Device.hpp"
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <thread>

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::gpudriven
{
    void GPUDrivenRenderer::collectShadowVisibleLights(std::unordered_set<uint32_t>& outLights, bool& outHasFilter)
    {
        outHasFilter = false;
        outLights.clear();

        if (lightCulling.useBVH && !lightCulling.visibleLightIds.empty())
        {
            outLights = lightCulling.visibleLightIds;
            outHasFilter = true;
        }

        if (lightCulling.useOcclusion && lightCulling.hasPrevFrameOcclusionData && !lightCulling.prevFrameOccludedLights.empty())
        {
            if (outHasFilter)
            {
                for (uint32_t occludedId : lightCulling.prevFrameOccludedLights)
                    outLights.erase(occludedId);
            }
            else
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                for (auto entity : registry.view<components::PointLightComponent>())
                {
                    uint32_t entityId = static_cast<uint32_t>(entity);
                    if (!lightCulling.prevFrameOccludedLights.contains(entityId))
                        outLights.insert(entityId);
                }
                for (auto entity : registry.view<components::SpotLightComponent>())
                {
                    uint32_t entityId = static_cast<uint32_t>(entity);
                    if (!lightCulling.prevFrameOccludedLights.contains(entityId))
                        outLights.insert(entityId);
                }
                outHasFilter = true;
            }
        }
    }

    void GPUDrivenRenderer::buildAndDispatchLightOcclusion(vk::CommandBuffer cmd)
    {
        if (!lightCulling.useOcclusion || !lightOcclusionCulling || !lightOcclusionCulling->isInitialized())
            return;

        std::vector<occlusion::GPULightBounds> lightBounds;
        auto& registry = scene::EntityRegistry::getRegistry();

        auto pointView = registry.view<components::PointLightComponent, components::WorldTransformComponent>();
        for (auto entity : pointView)
        {
            uint32_t entityId = static_cast<uint32_t>(entity);
            if (lightCulling.useBVH && !lightCulling.visibleLightIds.empty() && !lightCulling.visibleLightIds.contains(entityId))
                continue;

            const auto& light = pointView.get<components::PointLightComponent>(entity);
            const auto& transform = pointView.get<components::WorldTransformComponent>(entity);
            glm::vec3 position = glm::vec3(transform.worldMatrix[3]);

            occlusion::GPULightBounds bounds{};
            bounds.positionRadius = glm::vec4(position, light.radius);
            bounds.direction = glm::vec4(0.0f);
            bounds.entityId = entityId;
            bounds.lightType = static_cast<uint32_t>(occlusion::LightOcclusionType::Point);
            lightBounds.push_back(bounds);
        }

        auto spotView = registry.view<components::SpotLightComponent, components::WorldTransformComponent>();
        for (auto entity : spotView)
        {
            uint32_t entityId = static_cast<uint32_t>(entity);
            if (lightCulling.useBVH && !lightCulling.visibleLightIds.empty() && !lightCulling.visibleLightIds.contains(entityId))
                continue;

            const auto& light = spotView.get<components::SpotLightComponent>(entity);
            const auto& transform = spotView.get<components::WorldTransformComponent>(entity);
            glm::vec3 position = glm::vec3(transform.worldMatrix[3]);
            glm::vec3 forward = glm::normalize(glm::vec3(transform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

            occlusion::GPULightBounds bounds{};
            bounds.positionRadius = glm::vec4(position, light.range);
            bounds.direction = glm::vec4(forward, light.outerAngle);
            bounds.entityId = entityId;
            bounds.lightType = static_cast<uint32_t>(occlusion::LightOcclusionType::Spot);
            lightBounds.push_back(bounds);
        }

        if (!lightBounds.empty())
        {
            lightOcclusionCulling->updateLights(lightBounds);
            lightOcclusionCulling->recordLightUpload(cmd);

            const auto& camData = cameraBuffer->getData();
            glm::mat4 viewProj = camData.projection * camData.view;
            lightOcclusionCulling->updateCamera(viewProj, glm::vec3(camData.cameraPosition), camData.cameraPosition.w);

            lightOcclusionCulling->cull(cmd);
            lightOcclusionCulling->copyResultsToStaging(cmd);
        }
    }

    void GPUDrivenRenderer::recordPerViewShadowCull(vk::CommandBuffer cmd)
    {
        if (!shadowCullEnabled || !shadowCullManager || !shadowCullManager->isInitialized()) return;
        if (!shadowSystem || !shadowSystem->isShadowsEnabled()) return;
        if (shadowSystem->getActiveShadowViewCount() == 0 || stats.totalObjects == 0) return;

        // Global view order MUST match getShadowViewIndex / the page-build slot computation:
        // directional clipmap levels, then point cube faces, then spot views.
        std::vector<glm::mat4> viewProjections;
        viewProjections.reserve(shadowSystem->getActiveShadowViewCount());
        for (const auto& v : shadowSystem->getDirectionalShadowViews()) viewProjections.push_back(v.viewProjectionMatrix);
        for (const auto& v : shadowSystem->getPointShadowViews())       viewProjections.push_back(v.viewProjectionMatrix);
        for (const auto& v : shadowSystem->getSpotShadowViews())        viewProjections.push_back(v.viewProjectionMatrix);

        if (shadowCullManager->beginFrame(viewProjections, stats.totalObjects) == 0) return;
        shadowCullManager->recordCull(cmd, stats.totalObjects);
    }

    void GPUDrivenRenderer::recordShadowPasses(vk::CommandBuffer cmd, bool hasMeshObjects, bool hasTerrainTiles)
    {
        if (!shadowSystem || !shadowSystem->isShadowsEnabled()) return;

        // Early exit: skip entire shadow pipeline when no active shadow views exist
        if (shadowSystem->getActiveShadowViewCount() == 0) return;

        shadowSystem->uploadToGPU(cmd);

        shadow::ShadowPassParams shadowParams{};
        if (hasMeshObjects && meshShaderPipeline && boneMatrixManager && batchManager)
        {
            // Tier 4: when per-view shadow culling produced lists this frame, bind the compacted
            // shadow perDrawData set + buffers and let the recorder issue one draw per page. Else
            // fall back to the legacy main-camera buffer + batch x shaderGroup section loop.
            const bool perView = shadowCullEnabled && shadowCullManager &&
                                 shadowCullManager->isInitialized() && shadowCullManager->hasActiveViews();

            shadowParams.perDrawDataDescSet = perView
                ? shadowCullManager->getPerDrawDataDescSet()
                : meshShaderPipeline->getPerDrawDataDescriptorSet();
            shadowParams.meshletDataDescSet = meshShaderPipeline->getMeshletDataDescriptorSet();
            shadowParams.vertexDataDescSet = meshShaderPipeline->getVertexDataDescriptorSet();
            shadowParams.boneMatrixDescSet = boneMatrixManager->getDescriptorSet();
            shadowParams.cameraDescSet = shadowSystem->getShadowCameraDescSet();
            shadowParams.drawCommandBuffer = batchManager->getCombinedDrawCommandBuffer();
            shadowParams.drawCountBuffer = batchManager->getCombinedDrawCountBuffer();
            shadowParams.batchCount = batchManager->getBatchCount();
            shadowParams.commandsPerSection = batchManager->getCommandsPerSection();
            shadowParams.shaderGroupCount = batchManager->getShaderGroupCount();
            shadowParams.transparentGroupIndex = SHADER_GROUP_TRANSPARENT;
            shadowParams.drawCountStructSize = sizeof(BatchDrawStats);

            if (perView)
            {
                shadowParams.usePerViewShadowCull = true;
                shadowParams.shadowCullDrawCommandBuffer = shadowCullManager->getDrawCommandBuffer();
                shadowParams.shadowCullDrawCountBuffer = shadowCullManager->getDrawCountBuffer();
                shadowParams.shadowCullDrawsPerView = ShadowCullManager::drawsPerView();
                shadowParams.shadowCullActiveViews = shadowCullManager->getActiveViewCount();
            }
        }

        shadow::TerrainShadowPassParams terrainShadowParams{};
        shadow::TerrainShadowPassParams* terrainShadowParamsPtr = nullptr;

        if (hasTerrainTiles && terrain.pipeline && terrain.meshBuffer &&
            terrain.meshBuffer->isInitialized() && terrain.pipeline->getCurrentTileCount() > 0)
        {
            vk::DescriptorSet terrainDataSet = terrain.pipeline->getTerrainDataDescriptorSet();
            vk::DescriptorSet terrainMeshletSet = terrain.pipeline->getTerrainMeshletDescriptorSet();
            vk::DescriptorSet terrainVertexSet = terrain.pipeline->getTerrainVertexDescriptorSet();

            if (terrainDataSet && terrainMeshletSet && terrainVertexSet)
            {
                terrainShadowParams.terrainDataDescSet = terrainDataSet;
                terrainShadowParams.terrainMeshletDescSet = terrainMeshletSet;
                terrainShadowParams.terrainVertexDescSet = terrainVertexSet;
                terrainShadowParams.tileCount = terrain.pipeline->getCurrentTileCount();
                terrainShadowParamsPtr = &terrainShadowParams;
            }
        }

        shadowSystem->recordShadowPass(cmd, shadowParams, terrainShadowParamsPtr);
    }

    void GPUDrivenRenderer::updateLightCullingState(vk::CommandBuffer cmd)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        uint32_t pointCount = static_cast<uint32_t>(registry.view<components::PointLightComponent>().size());
        uint32_t spotCount = static_cast<uint32_t>(registry.view<components::SpotLightComponent>().size());
        lightCulling.totalSceneLights = pointCount + spotCount;

        if (lightCulling.useBVH && !lightCulling.visibleLightIds.empty())
            lightCulling.lightsAfterBVHCull = std::min(static_cast<uint32_t>(lightCulling.visibleLightIds.size()), lightCulling.totalSceneLights);
        else
            lightCulling.lightsAfterBVHCull = lightCulling.totalSceneLights;

        lightCulling.lightsAfterHiZCull = lightCulling.lightsAfterBVHCull;

        if (shadowSystem && shadowSystem->isInitialized())
        {
            // When RT directional shadows produced a mask last frame, the fragment shaders
            // override the directional VSM clipmap full-screen — so skip rendering its
            // (unsampled) pages this frame. Gate on the runtime flag (true only when RT
            // actually ran), not the rtShadowEnabled setting, so we never drop shadows while
            // RT is enabled-but-not-yet-ready.
            shadowSystem->setDirectionalRTOverrideActive(
                lightBufferManager && lightBufferManager->getRTShadowActive());

            std::unordered_set<uint32_t> shadowVisibleLights;
            bool hasShadowFilter = false;
            collectShadowVisibleLights(shadowVisibleLights, hasShadowFilter);

            if (hasShadowFilter && !shadowVisibleLights.empty())
                shadowSystem->beginFrame(cachedCamera.view, cachedCamera.projection,
                                          cachedCamera.nearPlane, cachedCamera.farPlane, &shadowVisibleLights);
            else
                shadowSystem->beginFrame(cachedCamera.view, cachedCamera.projection,
                                          cachedCamera.nearPlane, cachedCamera.farPlane);
        }

        if (lightBufferManager)
        {
            if (lightCulling.useBVH && !lightCulling.visibleLightIds.empty())
                lightBufferManager->updateFromScene(lightCulling.visibleLightIds);
            else
                lightBufferManager->updateFromScene();
            lightBufferManager->uploadToGPU(cmd);
        }
    }

    void GPUDrivenRenderer::dispatchVolumetricFog(vk::CommandBuffer cmd)
    {
        if (!volumetricPipeline || !volumetricPipeline->isEnabled()) return;

        const auto& camData = cameraBuffer->getData();
        glm::mat4 viewProj = camData.projection * camData.view;
        glm::mat4 invViewProj = glm::inverse(viewProj);
        volumetricPipeline->update(viewProj, invViewProj,
                                   glm::vec3(camData.cameraPosition),
                                   cachedCamera.nearPlane, cachedCamera.farPlane,
                                   cachedVolumetricSettings);
        // Update fog volumes from scene before dispatch
        if (fogVolumeBufferManager && fogVolumeBufferManager->isInitialized())
        {
            fogVolumeBufferManager->updateFromScene();
            fogVolumeBufferManager->uploadToGPU();
        }

        volumetricPipeline->dispatch(cmd,
                                     clusterGridManager->getDescriptorSet(),
                                     lightBufferManager->getDescriptorSet(),
                                     lightCullingPipeline->getDescriptorSet(),
                                     shadowSystem ? shadowSystem->getShadowDataDescSet() : vk::DescriptorSet{},
                                     shadowSystem ? shadowSystem->getShadowTextureDescSet() : vk::DescriptorSet{},
                                     fogVolumeBufferManager ? fogVolumeBufferManager->getDescriptorSet() : vk::DescriptorSet{},
                                     (giCascadeManager && giCascadeManager->getProbeStorage()
                                         && giCascadeManager->getProbeStorage()->isInitialized())
                                         ? giCascadeManager->getProbeStorage()->getComputeSamplingDescSet()
                                         : vk::DescriptorSet{});
    }

    void GPUDrivenRenderer::initLightStreaming(const lighting::LightStreamingConfig& config)
    {
        lightStreamManager = std::make_unique<lighting::LightStreamManager>();
        lightStreamManager->init(config);
    }

    void GPUDrivenRenderer::initObjectStreaming(const gpudriven::ObjectStreamConfig& config)
    {
        if (mergedBuffer)
        {
            objectStreamManager = std::make_unique<gpudriven::GPUObjectStreamManager>(*mergedBuffer);
            objectStreamManager->init(config);
        }
    }

    void GPUDrivenRenderer::dispatchCompute(vk::CommandBuffer cmd, uint32_t imageIndex)
    {
        if (!initialized || !enabled) return;

        // Lazy-init profiler if RT shadow pipeline exists (or will be created this frame).
        if (!rtShadowProfiler && rtShadowEnabled && accelStructManager && accelStructManager->isInitialized())
        {
            rtShadowProfiler = std::make_unique<raytracing::RTShadowProfiler>();
            rtShadowProfiler->init(device);
        }

        // Reset profiler query pool before any timestamp writes (matches dispatchGraphicsCompute path).
        if (rtShadowProfiler && rtShadowProfiler->isValid())
        {
            uint32_t profilerFI = imageIndex % core::MAX_FRAMES_IN_FLIGHT;
            rtShadowProfiler->resetFrame(cmd, profilerFI);
        }

        if (mergedBuffer) mergedBuffer->flushPendingTransfers();
        if (meshletBuffer) meshletBuffer->flushPendingTransfers();
        if (terrain.meshBuffer) terrain.meshBuffer->flushPendingTransfers();

        batchManager->resetAllBatches(cmd);
        if (meshShaderPipeline) meshShaderPipeline->resetStats(cmd);

        updateLightCullingState(cmd);

        bool hasMeshObjects = stats.totalObjects > 0;
        bool hasTerrainTiles = terrain.renderingEnabled && terrain.pipeline &&
                               terrain.pipeline->getCurrentTileCount() > 0;

        if (!hasMeshObjects && !hasTerrainTiles) return;

        if (hasMeshObjects)
        {
            if (mergedBuffer->isPersistentMode())
            {
                mergedBuffer->uploadDirtyObjects(cmd);
                mergedBuffer->uploadActiveIndices(cmd);
            }
            else
            {
                mergedBuffer->uploadObjects(cmd);
            }
            if (mergedBuffer->getInstanceCount() > 0)
                mergedBuffer->uploadInstances(cmd);
        }

        if (boneMatrixManager) boneMatrixManager->uploadToGPU(cmd);
        buildAndDispatchLightOcclusion(cmd);
        if (clusterGridManager) clusterGridManager->uploadToGPU(cmd);

        vk::MemoryBarrier memBarrier{
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
        };
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                            vk::DependencyFlags{}, 1, &memBarrier, 0, nullptr, 0, nullptr);

        if (lightCullingPipeline && lightBufferManager)
        {
            lightCullingPipeline->dispatch(cmd, cameraBuffer->getData().view,
                                           lightBufferManager->getPointLightCount(),
                                           lightBufferManager->getSpotLightCount());
        }

        {
            vk::DescriptorSet pickedCullSet = GPUDrivenRenderer::getThreadLocalCullDescriptorSet();
            if (pickedCullSet)
            {
                cullPipeline->dispatchWithSet(cmd, stats.totalObjects, pickedCullSet);
            }
            else
            {
                cullPipeline->dispatch(cmd, stats.totalObjects);
            }
        }
        batchManager->insertBarriersAfterCompute(cmd);

        // VK-1380: the shadow pass uses the process-wide shared ShadowSystem command pool. In an
        // RTT pass (e.g. the minimap render-to-texture) we must NOT re-run it. The main viewport
        // already reset, recorded and submitted this frame's VSM secondaries; re-running here
        // records into a *separate* RTT command buffer and resets those same secondaries while the
        // main submission is still pending -> "vkResetCommandBuffer is in use" /
        // "secondaries were destroyed or rerecorded" -> DEVICE_LOST. RTT scene draws sample the VSM
        // the main pass already produced (a top-down minimap does not need its own shadow map).
        const bool inRTTContext = GPUDrivenRenderer::getThreadLocalCullDescriptorSet() != nullptr;
        if (!inRTTContext)
        {
            // Tier 4: cull the scene per shadow view into the compacted shadow buffer before the
            // shadow pass records its draws. Same RTT guard as the shadow pass itself.
            recordPerViewShadowCull(cmd);
            recordShadowPasses(cmd, hasMeshObjects, hasTerrainTiles);
        }

        if (vegetation.grassInitialized && vegetation.grassRenderingEnabled)
            dispatchGrassCompute(cmd, vegetation.cachedVisibleTiles);

        dispatchVolumetricFog(cmd);
        dispatchGIProbeUpdate(cmd);

        if (lightStreamManager)
        {
            lightStreamManager->updatePriorities(cachedCamera.position);
            lightStreamManager->applyBudget();

            if (lightStreamManager->shouldDefragment() || lightStreamManager->isDefragInProgress())
                lightStreamManager->defragStep(4);
        }
    }
}
