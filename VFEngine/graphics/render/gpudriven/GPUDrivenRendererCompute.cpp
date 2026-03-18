#include "GPUDrivenRenderer.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Device.hpp"
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include <algorithm>

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
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
                auto pointView = registry.view<components::PointLightComponent>();
                for (auto entity : pointView)
                {
                    uint32_t entityId = static_cast<uint32_t>(entity);
                    if (!lightCulling.prevFrameOccludedLights.contains(entityId))
                        outLights.insert(entityId);
                }
                auto spotView = registry.view<components::SpotLightComponent>();
                for (auto entity : spotView)
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
        {
            return;
        }

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
        if (!shadowSystem || !shadowSystem->isShadowsEnabled())
        {
            return;
        }

        shadowSystem->uploadToGPU(cmd);

        shadow::ShadowPassParams shadowParams{};
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
                terrainShadowParams.shadowLOD = terrain.shadowLOD;
                terrainShadowParamsPtr = &terrainShadowParams;
            }
        }

        shadowSystem->recordShadowPass(cmd, shadowParams, terrainShadowParamsPtr);
    }

    void GPUDrivenRenderer::dispatchCompute(vk::CommandBuffer cmd)
    {
        if (!initialized || !enabled)
        {
            return;
        }

        if (mergedBuffer) mergedBuffer->flushPendingTransfers();
        if (meshletBuffer) meshletBuffer->flushPendingTransfers();
        if (terrain.meshBuffer) terrain.meshBuffer->flushPendingTransfers();

        batchManager->resetAllBatches(cmd);

        if (meshShaderPipeline)
        {
            meshShaderPipeline->resetStats(cmd);
        }

        updateLightCullingState(cmd);

        bool hasMeshObjects = stats.totalObjects > 0;
        bool hasTerrainTiles = terrain.renderingEnabled && terrain.pipeline &&
                               terrain.pipeline->getCurrentTileCount() > 0;

        if (!hasMeshObjects && !hasTerrainTiles)
        {
            return;
        }

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
            {
                mergedBuffer->uploadInstances(cmd);
            }
        }

        if (boneMatrixManager)
        {
            boneMatrixManager->uploadToGPU(cmd);
        }

        buildAndDispatchLightOcclusion(cmd);

        if (clusterGridManager)
        {
            clusterGridManager->uploadToGPU(cmd);
        }

        vk::MemoryBarrier memBarrier{
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
        };

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags{},
            1, &memBarrier,
            0, nullptr,
            0, nullptr);

        if (lightCullingPipeline && lightBufferManager)
        {
            lightCullingPipeline->dispatch(
                cmd,
                cameraBuffer->getData().view,
                lightBufferManager->getPointLightCount(),
                lightBufferManager->getSpotLightCount()
            );
        }

        cullPipeline->dispatch(cmd, stats.totalObjects);
        batchManager->insertBarriersAfterCompute(cmd);

        recordShadowPasses(cmd, hasMeshObjects, hasTerrainTiles);

        if (vegetation.grassInitialized && vegetation.grassRenderingEnabled)
        {
            dispatchGrassCompute(cmd, vegetation.cachedVisibleTiles);
        }

        dispatchVolumetricFog(cmd);
        dispatchGIProbeUpdate(cmd);

        // VSM page-based allocation - no per-frame LOD update needed

        if (lightStreamManager)
        {
            lightStreamManager->updatePriorities(cachedCamera.position);
            lightStreamManager->applyBudget();
        }
    }

    void GPUDrivenRenderer::updateLightCullingState(vk::CommandBuffer cmd)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        uint32_t pointCount = static_cast<uint32_t>(registry.view<components::PointLightComponent>().size());
        uint32_t spotCount = static_cast<uint32_t>(registry.view<components::SpotLightComponent>().size());
        lightCulling.totalSceneLights = pointCount + spotCount;

        if (lightCulling.useBVH && !lightCulling.visibleLightIds.empty())
        {
            lightCulling.lightsAfterBVHCull = std::min(static_cast<uint32_t>(lightCulling.visibleLightIds.size()), lightCulling.totalSceneLights);
        }
        else
        {
            lightCulling.lightsAfterBVHCull = lightCulling.totalSceneLights;
        }

        lightCulling.lightsAfterHiZCull = lightCulling.lightsAfterBVHCull;

        if (shadowSystem && shadowSystem->isInitialized())
        {
            std::unordered_set<uint32_t> shadowVisibleLights;
            bool hasShadowFilter = false;
            collectShadowVisibleLights(shadowVisibleLights, hasShadowFilter);

            if (hasShadowFilter && !shadowVisibleLights.empty())
            {
                shadowSystem->beginFrame(cachedCamera.view, cachedCamera.projection,
                                          cachedCamera.nearPlane, cachedCamera.farPlane,
                                          &shadowVisibleLights);
            }
            else
            {
                shadowSystem->beginFrame(cachedCamera.view, cachedCamera.projection,
                                          cachedCamera.nearPlane, cachedCamera.farPlane);
            }
        }

        if (lightBufferManager)
        {
            if (lightCulling.useBVH && !lightCulling.visibleLightIds.empty())
            {
                lightBufferManager->updateFromScene(lightCulling.visibleLightIds);
            }
            else
            {
                lightBufferManager->updateFromScene();
            }
            lightBufferManager->uploadToGPU(cmd);
        }
    }

    // Volumetric fog runs AFTER shadow passes so that shadow maps are rendered and available for sampling
    void GPUDrivenRenderer::dispatchVolumetricFog(vk::CommandBuffer cmd)
    {
        if (!volumetricPipeline || !volumetricPipeline->isEnabled())
        {
            return;
        }

        const auto& camData = cameraBuffer->getData();
        glm::mat4 viewProj = camData.projection * camData.view;
        glm::mat4 invViewProj = glm::inverse(viewProj);
        volumetricPipeline->update(viewProj, invViewProj,
                                   glm::vec3(camData.cameraPosition),
                                   cachedCamera.nearPlane, cachedCamera.farPlane,
                                   cachedVolumetricSettings);
        volumetricPipeline->dispatch(cmd,
                                     clusterGridManager->getDescriptorSet(),
                                     lightBufferManager->getDescriptorSet(),
                                     lightCullingPipeline->getDescriptorSet(),
                                     shadowSystem ? shadowSystem->getShadowDataDescSet() : vk::DescriptorSet{},
                                     shadowSystem ? shadowSystem->getShadowTextureDescSet() : vk::DescriptorSet{});
    }

    void GPUDrivenRenderer::dispatchGIProbeUpdate(vk::CommandBuffer cmd)
    {
        if (!giCascadeManager || !giCascadeManager->isInitialized() ||
            !giTracePipeline || !giTracePipeline->isInitialized())
        {
            return;
        }

        giCascadeManager->updateCameraPosition(cachedCamera.position);
        giCascadeManager->beginFrame();

        if (accelStructManager && accelStructManager->isInitialized() && mergedBuffer)
        {
            if (blasNeedsRebuild && mergedBuffer->getTotalVertexCount() > 0 &&
                mergedBuffer->getTotalIndexCount() > 0)
            {
                accelStructManager->buildBLAS(cmd,
                    mergedBuffer->getVertexBuffer(), mergedBuffer->getTotalVertexCount(), 64,
                    mergedBuffer->getIndexBuffer(), mergedBuffer->getTotalIndexCount());
                blasNeedsRebuild = false;
            }

            if (accelStructManager->isTLASReady() || !blasNeedsRebuild)
            {
                if (mergedBuffer->getObjectCount() > 0)
                {
                    accelStructManager->buildTLAS(cmd,
                        mergedBuffer->getCPUObjectData(),
                        mergedBuffer->getObjectCount());
                }
            }
        }

        auto* storage = giCascadeManager->getProbeStorage();

        if (storage && giProbeBuffersNeedInit)
        {
            storage->uploadToGPU(cmd);
            giProbeBuffersNeedInit = false;

            vk::MemoryBarrier initBarrier{
                vk::AccessFlagBits::eTransferWrite,
                vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
            };
            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eComputeShader,
                vk::DependencyFlags{},
                1, &initBarrier, 0, nullptr, 0, nullptr);
        }

        auto batches = giCascadeManager->getProbeUpdateBatches();

        if (!storage || batches.empty())
        {
            return;
        }

        vk::MemoryBarrier giBarrier{
            vk::AccessFlagBits::eShaderWrite,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags{},
            1, &giBarrier, 0, nullptr, 0, nullptr);

        for (const auto& batch : batches)
        {
            gi::GIComputePushConstants push{};
            push.cascadeIndex = batch.cascadeIndex;
            push.probeStartIndex = batch.probeStartOffset;
            push.probeCount = batch.probeCount;
            push.raysPerProbe = batch.isFarField
                ? cachedGISettings.farFieldRaysPerUpdate
                : cachedGISettings.probeRaysPerUpdate;
            push.maxDistance = batch.isFarField
                ? cachedGISettings.farFieldMaxDistance
                : cachedGISettings.maxProbeDistance;
            push.temporalBlend = cachedGISettings.temporalBlendFactor;
            push.frameRandom = static_cast<float>(giCascadeManager->getFrameIndex()) * 0.1f;
            push.frameIndex = giCascadeManager->getFrameIndex();

            vk::DescriptorSet tlasSet = (accelStructManager && accelStructManager->isTLASReady())
                ? accelStructManager->getTLASDescriptorSet()
                : vk::DescriptorSet{};

            vk::DescriptorSet lightSet = lightBufferManager
                ? lightBufferManager->getDescriptorSet()
                : vk::DescriptorSet{};

            giTracePipeline->dispatch(cmd,
                                       storage->getProbeDataDescSet(),
                                       giCascadeManager->getCascadeInfoDescSet(),
                                       push,
                                       tlasSet,
                                       lightSet);
        }

        vk::MemoryBarrier computeBarrier{
            vk::AccessFlagBits::eShaderWrite,
            vk::AccessFlagBits::eTransferRead
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eTransfer,
            vk::DependencyFlags{},
            1, &computeBarrier, 0, nullptr, 0, nullptr);

        storage->swapBuffers();

        {
            constexpr vk::DeviceSize probeSize = sizeof(gi::ProbeData);
            uint32_t totalProbes = storage->getProbeCount();

            std::vector<std::pair<uint32_t, uint32_t>> updatedRanges;
            updatedRanges.reserve(batches.size());
            for (const auto& batch : batches)
            {
                updatedRanges.emplace_back(batch.probeStartOffset, batch.probeStartOffset + batch.probeCount);
            }
            std::sort(updatedRanges.begin(), updatedRanges.end());

            std::vector<vk::BufferCopy> copyRegions;
            uint32_t cursor = 0;
            for (const auto& [start, end] : updatedRanges)
            {
                if (start > cursor)
                {
                    copyRegions.push_back({cursor * probeSize, cursor * probeSize, (start - cursor) * probeSize});
                }
                cursor = std::max(cursor, end);
            }
            if (cursor < totalProbes)
            {
                copyRegions.push_back({cursor * probeSize, cursor * probeSize, (totalProbes - cursor) * probeSize});
            }

            if (!copyRegions.empty())
            {
                cmd.copyBuffer(storage->getReadBuffer(), storage->getWriteBuffer(),
                               static_cast<uint32_t>(copyRegions.size()), copyRegions.data());
            }
        }

        vk::MemoryBarrier copyBarrier{
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags{},
            1, &copyBarrier, 0, nullptr, 0, nullptr);

        auto samplingSet = storage->getSamplingDescSet();
        if (meshShaderPipeline)
            meshShaderPipeline->updateGIProbeDescriptor(samplingSet);
        if (transparentMeshShaderPipeline)
            transparentMeshShaderPipeline->updateGIProbeDescriptor(samplingSet);
        if (wboitMeshShaderPipeline)
            wboitMeshShaderPipeline->updateGIProbeDescriptor(samplingSet);
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

    void GPUDrivenRenderer::initGI(const gi::GISettings& settings)
    {
        cachedGISettings = settings;

        if (!settings.enabled || settings.quality == gi::GIQuality::Off)
        {
            return;
        }

        if (settings.quality >= gi::GIQuality::Medium)
        {
            giCascadeManager = std::make_unique<gi::RadianceCascadeManager>(device);
            giCascadeManager->init(settings);

            auto* storage = giCascadeManager->getProbeStorage();
            if (storage && storage->isInitialized())
            {
                vk::DescriptorSetLayout tlasLayout = nullptr;
                if (device.isRayQuerySupported())
                {
                    accelStructManager = std::make_unique<gi::AccelerationStructureManager>(device);
                    accelStructManager->init();
                    if (accelStructManager->isInitialized())
                    {
                        tlasLayout = accelStructManager->getTLASDescriptorLayout();
                        blasNeedsRebuild = true;
                    }
                }

                vk::DescriptorSetLayout lightDataLayout = lightBufferManager
                    ? lightBufferManager->getDescriptorSetLayout() : nullptr;

                giTracePipeline = std::make_unique<gi::ProbeTracePipeline>(device);
                giTracePipeline->init(storage->getProbeDataLayout(),
                                       storage->getCascadeInfoLayout(),
                                       tlasLayout,
                                       lightDataLayout);

                giUpdatePipeline = std::make_unique<gi::ProbeUpdatePipeline>(device);
                giUpdatePipeline->init(storage->getProbeDataLayout(),
                                        storage->getCascadeInfoLayout());

                if (cachedRenderPass)
                {
                    giDebugRenderer = std::make_unique<gi::GIDebugRenderer>(device);
                    giDebugRenderer->init(cachedRenderPass,
                                           storage->getProbeDataLayout(),
                                           storage->getCascadeInfoLayout());
                }

                if (meshShaderPipeline && shadowSystem)
                {
                    MeshPipelineInitInfo pipelineInfo{
                        .iblLayout = cachedIBLLayout,
                        .bindlessTextureLayout = bindlessTextures->getDescriptorSetLayout(),
                        .boneMatrixLayout = boneMatrixManager->getDescriptorSetLayout(),
                        .lightDataLayout = lightBufferManager->getDescriptorSetLayout(),
                        .clusterGridLayout = clusterGridManager->getDescriptorSetLayout(),
                        .cullingOutputLayout = lightCullingPipeline->getDescriptorSetLayout(),
                        .shadowDataLayout = shadowSystem->getShadowDataLayout(),
                        .shadowTextureLayout = shadowSystem->getShadowTextureLayout(),
                        .giProbeDataLayout = storage->getSamplingLayout(),
                        .renderPass = cachedRenderPass
                    };

                    meshShaderPipeline->recreate(pipelineInfo);
                    meshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());

                    if (transparentMeshShaderPipeline)
                    {
                        pipelineInfo.transparentMode = true;
                        transparentMeshShaderPipeline->recreate(pipelineInfo);
                        transparentMeshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                        pipelineInfo.transparentMode = false;
                    }

                    if (wboitMeshShaderPipeline && cachedWBOITRenderPass)
                    {
                        pipelineInfo.renderPass = cachedWBOITRenderPass;
                        pipelineInfo.wboitMode = true;
                        wboitMeshShaderPipeline->recreate(pipelineInfo);
                        wboitMeshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                    }
                }
            }
        }
    }

    void GPUDrivenRenderer::cleanupGI()
    {
        device.getLogicalDevice().waitIdle();

        if (meshShaderPipeline)
            meshShaderPipeline->updateGIProbeDescriptor(vk::DescriptorSet{});
        if (transparentMeshShaderPipeline)
            transparentMeshShaderPipeline->updateGIProbeDescriptor(vk::DescriptorSet{});
        if (wboitMeshShaderPipeline)
            wboitMeshShaderPipeline->updateGIProbeDescriptor(vk::DescriptorSet{});

        if (giDebugRenderer) { giDebugRenderer->cleanup(); giDebugRenderer.reset(); }
        if (giUpdatePipeline) { giUpdatePipeline->cleanup(); giUpdatePipeline.reset(); }
        if (giTracePipeline) { giTracePipeline->cleanup(); giTracePipeline.reset(); }
        if (accelStructManager) { accelStructManager->cleanup(); accelStructManager.reset(); }
        if (giCascadeManager) { giCascadeManager->cleanup(); giCascadeManager.reset(); }
        blasNeedsRebuild = true;
        giProbeBuffersNeedInit = true;
    }

    void GPUDrivenRenderer::applyGISettings(const gi::GISettings& settings)
    {
        bool needsReinit = (settings.quality != cachedGISettings.quality) ||
                           (settings.enabled != cachedGISettings.enabled) ||
                           (settings.probeSpacing != cachedGISettings.probeSpacing);

        cachedGISettings = settings;

        if (needsReinit)
        {
            cleanupGI();
            initGI(settings);

            if (!giCascadeManager && meshShaderPipeline && shadowSystem && cachedRenderPass)
            {
                MeshPipelineInitInfo pipelineInfo{
                    .iblLayout = cachedIBLLayout,
                    .bindlessTextureLayout = bindlessTextures->getDescriptorSetLayout(),
                    .boneMatrixLayout = boneMatrixManager->getDescriptorSetLayout(),
                    .lightDataLayout = lightBufferManager->getDescriptorSetLayout(),
                    .clusterGridLayout = clusterGridManager->getDescriptorSetLayout(),
                    .cullingOutputLayout = lightCullingPipeline->getDescriptorSetLayout(),
                    .shadowDataLayout = shadowSystem->getShadowDataLayout(),
                    .shadowTextureLayout = shadowSystem->getShadowTextureLayout(),
                    .giProbeDataLayout = nullptr,
                    .renderPass = cachedRenderPass
                };
                meshShaderPipeline->recreate(pipelineInfo);
                if (transparentMeshShaderPipeline)
                {
                    pipelineInfo.transparentMode = true;
                    transparentMeshShaderPipeline->recreate(pipelineInfo);
                    pipelineInfo.transparentMode = false;
                }
                if (wboitMeshShaderPipeline && cachedWBOITRenderPass)
                {
                    pipelineInfo.renderPass = cachedWBOITRenderPass;
                    pipelineInfo.wboitMode = true;
                    wboitMeshShaderPipeline->recreate(pipelineInfo);
                }
            }
        }
        else if (giCascadeManager)
        {
            giCascadeManager->applySettings(settings);
        }

        if (giDebugRenderer)
        {
            giDebugRenderer->setShowProbes(settings.showProbes);
            giDebugRenderer->setShowCascadeBounds(settings.showCascadeBounds);
            giDebugRenderer->setShowProbeValidity(settings.showProbeValidity);
        }
    }

    void GPUDrivenRenderer::dispatchGraphicsCompute(vk::CommandBuffer cmd)
    {
        if (!initialized || !enabled)
        {
            return;
        }

        if (mergedBuffer) mergedBuffer->flushPendingTransfers();
        if (meshletBuffer) meshletBuffer->flushPendingTransfers();
        if (terrain.meshBuffer) terrain.meshBuffer->flushPendingTransfers();

        batchManager->resetAllBatches(cmd);

        if (meshShaderPipeline)
        {
            meshShaderPipeline->resetStats(cmd);
        }

        updateLightCullingState(cmd);

        bool hasMeshObjects = stats.totalObjects > 0;
        bool hasTerrainTiles = terrain.renderingEnabled && terrain.pipeline &&
                               terrain.pipeline->getCurrentTileCount() > 0;

        if (!hasMeshObjects && !hasTerrainTiles)
        {
            return;
        }

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
            {
                mergedBuffer->uploadInstances(cmd);
            }
        }

        if (boneMatrixManager)
        {
            boneMatrixManager->uploadToGPU(cmd);
        }

        buildAndDispatchLightOcclusion(cmd);

        if (clusterGridManager)
        {
            clusterGridManager->uploadToGPU(cmd);
        }

        // Transfer -> Compute barrier (uploads complete)
        vk::MemoryBarrier memBarrier{
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
        };

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags{},
            1, &memBarrier,
            0, nullptr,
            0, nullptr);

        // Object culling stays on graphics queue (output feeds shadow passes)
        cullPipeline->dispatch(cmd, stats.totalObjects);
        batchManager->insertBarriersAfterCompute(cmd);

        recordShadowPasses(cmd, hasMeshObjects, hasTerrainTiles);

        // Volumetric fog stays on graphics queue (depends on shadow maps)
        dispatchVolumetricFog(cmd);

        if (lightStreamManager)
        {
            lightStreamManager->updatePriorities(cachedCamera.position);
            lightStreamManager->applyBudget();
        }
    }

    void GPUDrivenRenderer::dispatchAsyncCompute(vk::CommandBuffer asyncCmd)
    {
        if (!initialized || !enabled)
        {
            return;
        }

        // Light culling - independent of object culling, reads uploaded light/cluster buffers
        if (lightCullingPipeline && lightBufferManager)
        {
            lightCullingPipeline->dispatch(
                asyncCmd,
                cameraBuffer->getData().view,
                lightBufferManager->getPointLightCount(),
                lightBufferManager->getSpotLightCount()
            );
        }

        // Grass compute - independent, reads terrain tile data
        if (vegetation.grassInitialized && vegetation.grassRenderingEnabled)
        {
            dispatchGrassCompute(asyncCmd, vegetation.cachedVisibleTiles);
        }

        // GI probe trace - independent, reads TLAS and writes probes
        dispatchGIProbeUpdate(asyncCmd);
    }

}
