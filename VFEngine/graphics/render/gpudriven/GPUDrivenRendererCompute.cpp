#include "GPUDrivenRenderer.hpp"
// VK-1443: full manager types (forward-declared in GPUDrivenRenderer.hpp) dereferenced
// by the compute dispatch paths in this TU.
#include "../occlusion/LightOcclusionCulling.hpp"
#include "../volumetric/FogVolumeBufferManager.hpp"
#include "../raytracing/AccelerationStructureManager.hpp"
#include "../raytracing/RTShadowProfiler.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Device.hpp"
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cstring>
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

    void GPUDrivenRenderer::recordShadowPasses(vk::CommandBuffer cmd, bool hasMeshObjects, bool hasTerrainTiles)
    {
        if (!shadowSystem || !shadowSystem->isShadowsEnabled()) return;

        // Early exit: skip entire shadow pipeline when no active shadow views exist
        if (shadowSystem->getActiveShadowViewCount() == 0) return;

        shadowSystem->uploadToGPU(cmd);

        shadow::ShadowPassParams shadowParams{};
        // A4: per-section occupancy so the legacy shadow loop skips empty sections. Kept alive for
        // the whole recordShadowPass call below (same scope as shadowParams).
        std::vector<uint8_t> shadowSectionOccupancy;
        if (hasMeshObjects && meshShaderPipeline && boneMatrixManager && batchManager)
        {
            shadowParams.perDrawDataDescSet = meshShaderPipeline->getPerDrawDataDescriptorSet();
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

            const uint32_t sgCount = batchManager->getShaderGroupCount();
            const uint32_t bCount = batchManager->getBatchCount();
            shadowSectionOccupancy.resize(static_cast<size_t>(bCount) * sgCount, 0);
            for (uint32_t batch = 0; batch < bCount; ++batch)
                for (uint32_t sg = 0; sg < sgCount; ++sg)
                    shadowSectionOccupancy[batch * sgCount + sg] =
                        batchManager->sectionHasCandidates(batch, sg) ? 1 : 0;
            shadowParams.sectionOccupancy = &shadowSectionOccupancy;
        }

        // B1: when the page-binned cull produced bins this frame, hand the recorder the bin buffers
        // + draw descriptor set. Directional bin pages draw from these; everything else (and any
        // overflow-fallback page) still uses the legacy fields above. Only the main pass bins (RTT
        // skips recordShadowPasses / the bin dispatch).
        if (shadowPageBinner && shadowPageBinner->isInitialized() &&
            shadowSystem->isShadowBinActive() &&
            GPUDrivenRenderer::getThreadLocalCullDescriptorSet() == nullptr)
        {
            shadowParams.shadowCullEnabled = true;
            shadowParams.binPerDrawDataDescSet = shadowPageBinner->getDrawDescriptorSet();
            shadowParams.binCommandBuffer = shadowPageBinner->getBinCommandBuffer();
            shadowParams.binCountBuffer = shadowPageBinner->getBinCountBuffer();
            shadowParams.binCapacity = shadowPageBinner->getBinCapacity();
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

            // A1: refresh dynamic shadow-caster bounds from resolved object data (mergedBuffer is
            // current here — updateScene ran before dispatchCompute) so determineDynamicPages marks
            // only overlapped pages inside the beginFrame call below.
            gatherDynamicShadowCasterBounds();

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

    void GPUDrivenRenderer::gatherDynamicShadowCasterBounds()
    {
        if (!shadowSystem) return;
        auto& dst = shadowSystem->mutableDynamicCasterBounds();
        dst.clear();
        if (!mergedBuffer) return;

        const auto& objects = mergedBuffer->getCPUObjectData();
        const auto& instanceXf = mergedBuffer->getCPUInstanceTransforms();

        // A dynamic caster = a shadow-casting object whose shadow is NOT cacheable-static. Skip
        // static (ShadowStatic) casters, terrain, and the non-opaque groups the shadow pass never
        // draws. Over-inclusion would only mark a few extra pages dynamic; under-inclusion would
        // drop a caster's dynamic shadow, so err toward including.
        constexpr uint32_t kSkipMask = ObjectFlags::ShadowStatic | ObjectFlags::TerrainTile |
                                       ObjectFlags::Translucent | ObjectFlags::AdditiveBlend |
                                       ObjectFlags::MultiplyBlend;

        auto consider = [&](uint32_t i)
        {
            if (i >= objects.size()) return;
            const auto& obj = objects[i];
            if (obj.flags & kSkipMask) return;

            const glm::vec3 localMin(obj.aabbMin);
            const glm::vec3 localMax(obj.aabbMax);

            if (obj.flags & ObjectFlags::Instanced)
            {
                uint32_t instanceCount = 0;
                std::memcpy(&instanceCount, &obj.aabbMax.w, sizeof(uint32_t)); // aabbMax.w is a uint, not float
                uint32_t instanceOffset = obj.instanceData.w;
                if (instanceCount > 1 &&
                    static_cast<size_t>(instanceOffset) + instanceCount <= instanceXf.size())
                {
                    for (uint32_t k = 0; k < instanceCount; ++k)
                        dst.push_back({localMin, localMax, instanceXf[instanceOffset + k].modelMatrix});
                    return;
                }
            }
            dst.push_back({localMin, localMax, obj.modelMatrix});
        };

        // Persistent (streaming) mode leaves stale data in freed slots, so iterate only active
        // slots; editor/non-streaming mode is dense [0, objectCount).
        if (mergedBuffer->isPersistentMode())
        {
            // getActiveObjectIndices() is sized to full buffer capacity
            // (maxObjectCount); only [0, activeObjectCount) holds live slots —
            // the tail is stale. Bound the scan the same way uploadActiveIndices
            // does, or it degenerates to O(maxObjectCount) per frame.
            const auto& activeIndices = mergedBuffer->getActiveObjectIndices();
            const uint32_t activeCount = mergedBuffer->getActiveObjectCount();
            for (uint32_t i = 0; i < activeCount; ++i)
                consider(activeIndices[i]);
        }
        else
        {
            const uint32_t count = mergedBuffer->getObjectCount();
            for (uint32_t i = 0; i < count; ++i)
                consider(i);
        }
    }

    bool GPUDrivenRenderer::shadowBinCullReady() const
    {
        if (!shadowPageBinner || !shadowPageBinner->isInitialized()) return false;
        if (!shadowSystem || !shadowSystem->isShadowBinActive()) return false;
        if (!mergedBuffer || stats.totalObjects == 0) return false;
        // Never bin in an RTT pre-pass (the shadow pass itself is skipped there — see the RTT guard
        // in dispatchCompute); binning would waste work and read a per-RTT camera.
        if (GPUDrivenRenderer::getThreadLocalCullDescriptorSet() != nullptr) return false;
        return !shadowSystem->getShadowBinViews().empty();
    }

    void GPUDrivenRenderer::prepareShadowBinCull(vk::CommandBuffer cmd)
    {
        if (!shadowBinCullReady()) return;

        shadowPageBinner->advanceStagingFrame();

        // B2: one GPU view per binned view (directional level / spot / point face).
        const auto& binViews = shadowSystem->getShadowBinViews();
        std::vector<ShadowLevelData> levels;
        levels.reserve(binViews.size());
        for (const auto& v : binViews)
        {
            ShadowLevelData ld{};
            ld.viewProjection = v.viewProjection;
            ld.bias = glm::vec4(v.depthBias, v.slopeBias, v.normalBias, v.lodBias); // .w = B3 per-view LOD bias
            ld.gridInfo = glm::uvec4(v.pagesX, v.pagesY, v.pageBaseOffset, 0u);
            levels.push_back(ld);
        }

        shadowPageBinner->updateFrameData(levels, shadowSystem->getShadowBinPageBase());
        // SAME handles the main cull binds (GPUDrivenRendererScene.cpp updateDescriptors) — the bin
        // cull must see exactly the objects/activeIndices the main cull did (anti-desync).
        shadowPageBinner->updateComputeDescriptors(
            mergedBuffer->getObjectBuffer(), cameraBuffer->getBuffer(),
            mergedBuffer->getInstanceTransformBuffer(), mergedBuffer->getActiveIndexBuffer());

        shadowPageBinner->recordReset(cmd);
        shadowPageBinner->recordUpload(cmd); // rides the caller's transfer->compute barrier
    }

    void GPUDrivenRenderer::dispatchShadowBinCull(vk::CommandBuffer cmd)
    {
        if (!shadowBinCullReady()) return;

        uint32_t flags = 0;
        flags |= 1u; // distance cull on (shadows respect category max-distance)
        // bit1 occlusion stays off in B1/B2 (camera Hi-Z is wrong for shadow views; B3 adds shadow Hi-Z)
        if (culling.lodSelectionEnabled) flags |= 4u;

        shadowPageBinner->dispatch(cmd, stats.totalObjects,
                                   static_cast<uint32_t>(shadowSystem->getShadowBinViews().size()), flags);
        shadowPageBinner->recordPostBarrier(cmd);
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
        // VK-1398: record the image index of this submission for per-image RT-mask ring binding.
        currentImageIndex = imageIndex;
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

        // A7: lazy-init the acceleration structures here too (the async path does it in
        // dispatchGraphicsCompute). Without this the sync path never creates the manager, so
        // buildAccelerationStructures below would no-op and RT shadows would stay dead.
        initAccelerationStructures();

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
                // VK-1418: repoint RTT-bound material slots to this image's bindless slot before
                // the dirty upload so the change rides the existing transfer→shader barrier.
                patchRenderTextureMaterialSlots(imageIndex);
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

        // B1: stage + reset the shadow bins before the barrier so the cull sees zeroed counts.
        prepareShadowBinCull(cmd);

        vk::MemoryBarrier memBarrier{
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
        };
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                            vk::DependencyFlags{}, 1, &memBarrier, 0, nullptr, 0, nullptr);

        // A7: build BLAS/TLAS on the sync path too (mirrors dispatchGraphicsCompute). Runs after the
        // transfer->compute barrier so vertex/index/object uploads are visible to the AS build.
        buildAccelerationStructures(cmd, imageIndex);

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
        // B1: cull into the shadow bins right after the main cull (same objectCount + activeIndices).
        dispatchShadowBinCull(cmd);
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
