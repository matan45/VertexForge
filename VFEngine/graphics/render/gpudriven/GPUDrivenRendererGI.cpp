#include "GPUDrivenRenderer.hpp"
#include "../occlusion/DepthPrepass.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Device.hpp"
#include "../../core/GraphicsConstants.hpp"
#include "types/RenderSettings.hpp"
#include "print/Log.hpp"
#include <algorithm>

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::gpudriven
{
    void GPUDrivenRenderer::dispatchGIProbeUpdate(vk::CommandBuffer cmd)
    {
        if (!giCascadeManager || !giCascadeManager->isInitialized() ||
            !giTracePipeline || !giTracePipeline->isInitialized())
            return;

        giCascadeManager->updateCameraPosition(cachedCamera.position);
        giCascadeManager->beginFrame();

        // BLAS/TLAS are now built in dispatchGraphicsCompute() before shadow passes.
        // GI just reads the shared TLAS descriptor set.

        auto* storage = giCascadeManager->getProbeStorage();

        if (storage && giProbeBuffersNeedInit)
        {
            storage->uploadToGPU(cmd);
            giProbeBuffersNeedInit = false;

            vk::MemoryBarrier initBarrier{vk::AccessFlagBits::eTransferWrite,
                vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite};
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                                vk::DependencyFlags{}, 1, &initBarrier, 0, nullptr, 0, nullptr);
        }

        auto batches = giCascadeManager->getProbeUpdateBatches();
        if (!storage || batches.empty()) return;

        vk::MemoryBarrier giBarrier{vk::AccessFlagBits::eShaderWrite,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                            vk::DependencyFlags{}, 1, &giBarrier, 0, nullptr, 0, nullptr);

        for (const auto& batch : batches)
        {
            gi::GIComputePushConstants push{};
            push.cascadeIndex = batch.cascadeIndex;
            push.probeStartIndex = batch.probeStartOffset;
            push.probeCount = batch.probeCount;
            push.raysPerProbe = batch.isFarField ? cachedGISettings.farFieldRaysPerUpdate : cachedGISettings.probeRaysPerUpdate;
            push.maxDistance = batch.isFarField ? cachedGISettings.farFieldMaxDistance : cachedGISettings.maxProbeDistance;
            push.temporalBlend = cachedGISettings.temporalBlendFactor;
            push.frameRandom = static_cast<float>(giCascadeManager->getFrameIndex()) * 0.1f;
            push.frameIndex = giCascadeManager->getFrameIndex();

            vk::DescriptorSet tlasSet = (accelStructManager && accelStructManager->isTLASReady())
                ? accelStructManager->getTLASDescriptorSet() : vk::DescriptorSet{};
            vk::DescriptorSet lightSet = lightBufferManager ? lightBufferManager->getDescriptorSet() : vk::DescriptorSet{};

            giTracePipeline->dispatch(cmd, storage->getProbeDataDescSet(),
                                       giCascadeManager->getCascadeInfoDescSet(), push, tlasSet, lightSet);
        }

        vk::MemoryBarrier computeBarrier{vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer,
                            vk::DependencyFlags{}, 1, &computeBarrier, 0, nullptr, 0, nullptr);

        storage->swapBuffers();

        {
            constexpr vk::DeviceSize probeSize = sizeof(gi::ProbeData);
            uint32_t totalProbes = storage->getProbeCount();

            std::vector<std::pair<uint32_t, uint32_t>> updatedRanges;
            updatedRanges.reserve(batches.size());
            for (const auto& batch : batches)
                updatedRanges.emplace_back(batch.probeStartOffset, batch.probeStartOffset + batch.probeCount);
            std::sort(updatedRanges.begin(), updatedRanges.end());

            std::vector<vk::BufferCopy> copyRegions;
            uint32_t cursor = 0;
            for (const auto& [start, end] : updatedRanges)
            {
                if (start > cursor)
                    copyRegions.push_back({cursor * probeSize, cursor * probeSize, (start - cursor) * probeSize});
                cursor = std::max(cursor, end);
            }
            if (cursor < totalProbes)
                copyRegions.push_back({cursor * probeSize, cursor * probeSize, (totalProbes - cursor) * probeSize});

            if (!copyRegions.empty())
                cmd.copyBuffer(storage->getReadBuffer(), storage->getWriteBuffer(),
                               static_cast<uint32_t>(copyRegions.size()), copyRegions.data());
        }

        vk::MemoryBarrier copyBarrier{vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader,
                            vk::DependencyFlags{}, 1, &copyBarrier, 0, nullptr, 0, nullptr);

        auto samplingSet = storage->getSamplingDescSet();
        if (meshShaderPipeline) meshShaderPipeline->updateGIProbeDescriptor(samplingSet);
        if (transparentMeshShaderPipeline) transparentMeshShaderPipeline->updateGIProbeDescriptor(samplingSet);
        if (wboitMeshShaderPipeline) wboitMeshShaderPipeline->updateGIProbeDescriptor(samplingSet);
    }

    void GPUDrivenRenderer::initGI(const gi::GISettings& settings)
    {
        cachedGISettings = settings;
        if (!settings.enabled || settings.quality == gi::GIQuality::Off) return;

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
                    ensureAccelerationStructureManager();
                    if (accelStructManager && accelStructManager->isInitialized())
                    {
                        tlasLayout = accelStructManager->getTLASDescriptorLayout();
                    }
                }

                vk::DescriptorSetLayout lightDataLayout = lightBufferManager
                    ? lightBufferManager->getDescriptorSetLayout() : nullptr;

                giTracePipeline = std::make_unique<gi::ProbeTracePipeline>(device);
                giTracePipeline->init(storage->getProbeDataLayout(), storage->getCascadeInfoLayout(), tlasLayout, lightDataLayout);

                giUpdatePipeline = std::make_unique<gi::ProbeUpdatePipeline>(device);
                giUpdatePipeline->init(storage->getProbeDataLayout(), storage->getCascadeInfoLayout());

                if (cachedRenderPass)
                {
                    giDebugRenderer = std::make_unique<gi::GIDebugRenderer>(device);
                    giDebugRenderer->init(cachedRenderPass, storage->getProbeDataLayout(), storage->getCascadeInfoLayout());
                }

                if (meshShaderPipeline && shadowSystem)
                {
                    vk::DescriptorSetLayout causticLayout{};
                    if (water.causticsResources && water.causticsResources->isInitialized())
                        causticLayout = water.causticsResources->getDescriptorSetLayout();

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
                        .causticLayout = causticLayout,
                        .renderPass = cachedRenderPass
                    };

                    meshShaderPipeline->recreate(pipelineInfo);
                    meshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                    if (causticLayout)
                        meshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());

                    if (transparentMeshShaderPipeline)
                    {
                        pipelineInfo.transparentMode = true;
                        transparentMeshShaderPipeline->recreate(pipelineInfo);
                        transparentMeshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                        if (causticLayout)
                            transparentMeshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());
                        pipelineInfo.transparentMode = false;
                    }

                    if (wboitMeshShaderPipeline && cachedWBOITRenderPass)
                    {
                        pipelineInfo.renderPass = cachedWBOITRenderPass;
                        pipelineInfo.wboitMode = true;
                        wboitMeshShaderPipeline->recreate(pipelineInfo);
                        wboitMeshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                        if (causticLayout)
                            wboitMeshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());
                    }
                }
            }
        }
    }

    void GPUDrivenRenderer::cleanupGI()
    {
        device.getLogicalDevice().waitIdle();

        if (meshShaderPipeline) meshShaderPipeline->updateGIProbeDescriptor(vk::DescriptorSet{});
        if (transparentMeshShaderPipeline) transparentMeshShaderPipeline->updateGIProbeDescriptor(vk::DescriptorSet{});
        if (wboitMeshShaderPipeline) wboitMeshShaderPipeline->updateGIProbeDescriptor(vk::DescriptorSet{});

        if (giDebugRenderer) { giDebugRenderer->cleanup(); giDebugRenderer.reset(); }
        if (giUpdatePipeline) { giUpdatePipeline->cleanup(); giUpdatePipeline.reset(); }
        if (giTracePipeline) { giTracePipeline->cleanup(); giTracePipeline.reset(); }
        if (rtShadowProfiler) { rtShadowProfiler->cleanup(device.getLogicalDevice()); rtShadowProfiler.reset(); }
        if (rtShadowDenoiser) { rtShadowDenoiser->cleanup(); rtShadowDenoiser.reset(); }
        if (rtShadowPipeline) { rtShadowPipeline->cleanup(); rtShadowPipeline.reset(); }
        if (accelStructManager) { accelStructManager->cleanup(); accelStructManager.reset(); }
        if (giCascadeManager) { giCascadeManager->cleanup(); giCascadeManager.reset(); }
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
                vk::DescriptorSetLayout causticLayout{};
                if (water.causticsResources && water.causticsResources->isInitialized())
                    causticLayout = water.causticsResources->getDescriptorSetLayout();

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
                    .causticLayout = causticLayout,
                    .renderPass = cachedRenderPass
                };
                meshShaderPipeline->recreate(pipelineInfo);
                if (causticLayout)
                    meshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());
                if (transparentMeshShaderPipeline)
                {
                    pipelineInfo.transparentMode = true;
                    transparentMeshShaderPipeline->recreate(pipelineInfo);
                    if (causticLayout)
                        transparentMeshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());
                    pipelineInfo.transparentMode = false;
                }
                if (wboitMeshShaderPipeline && cachedWBOITRenderPass)
                {
                    pipelineInfo.renderPass = cachedWBOITRenderPass;
                    pipelineInfo.wboitMode = true;
                    wboitMeshShaderPipeline->recreate(pipelineInfo);
                    if (causticLayout)
                        wboitMeshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());
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

    void GPUDrivenRenderer::dispatchGraphicsCompute(vk::CommandBuffer cmd, uint32_t imageIndex)
    {
        if (!initialized || !enabled) return;

        // Lazy-init profiler if RT shadow pipeline exists (or will be created this frame).
        // Must happen before any timestamp writes in this function or dispatchRTShadow.
        if (!rtShadowProfiler && rtShadowEnabled && accelStructManager && accelStructManager->isInitialized())
        {
            rtShadowProfiler = std::make_unique<raytracing::RTShadowProfiler>();
            rtShadowProfiler->init(device);
        }

        // Reset profiler query pool at the start of the frame (before any timestamp writes).
        // Both dispatchGraphicsCompute (BLAS/TLAS) and dispatchRTShadow write into this pool.
        if (rtShadowProfiler && rtShadowProfiler->isValid())
        {
            uint32_t profilerFI = imageIndex % core::MAX_FRAMES_IN_FLIGHT;
            rtShadowProfiler->resetFrame(cmd, profilerFI);
        }

        // Lazy init acceleration structures for RT shadows (independent of GI)
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

        vk::MemoryBarrier memBarrier{vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                            vk::DependencyFlags{}, 1, &memBarrier, 0, nullptr, 0, nullptr);

        // Build/update acceleration structures for RT shadows and GI
        if (accelStructManager && accelStructManager->isInitialized() && mergedBuffer)
        {
            bool hasPendingBLAS = accelStructManager->hasPendingBLASBuilds();
            bool hasPendingTerrainBLAS = accelStructManager->hasPendingTerrainBLASBuilds() && terrain.meshBuffer;
            uint32_t profilerFI = imageIndex % core::MAX_FRAMES_IN_FLIGHT;

            if (rtShadowProfiler && rtShadowProfiler->isValid() && (hasPendingBLAS || hasPendingTerrainBLAS))
            {
                rtShadowProfiler->writeTimestamp(cmd, profilerFI,
                    raytracing::RTShadowTimestamp::BeforeBLASBuild,
                    vk::PipelineStageFlagBits::eAllCommands);
            }

            if (hasPendingBLAS)
            {
                accelStructManager->buildPendingBLAS(cmd,
                    mergedBuffer->getVertexBuffer(), 64,
                    mergedBuffer->getIndexBuffer());
            }
            if (hasPendingTerrainBLAS)
            {
                accelStructManager->buildPendingTerrainBLAS(cmd,
                    terrain.meshBuffer->getVertexBuffer(), 64,
                    terrain.meshBuffer->getIndexBuffer());
            }

            if (rtShadowProfiler && rtShadowProfiler->isValid() && (hasPendingBLAS || hasPendingTerrainBLAS))
            {
                rtShadowProfiler->writeTimestamp(cmd, profilerFI,
                    raytracing::RTShadowTimestamp::AfterBLASBuild,
                    vk::PipelineStageFlagBits::eAllCommands);
                rtShadowProfiler->markBLASBuilt();
            }

            {
                bool hasTerrainAS = terrain.adapter && terrain.pipeline &&
                                    terrain.pipeline->getCurrentTileCount() > 0;

                if (hasTerrainAS)
                {
                    accelStructManager->buildTLASWithTerrain(cmd,
                        mergedBuffer->getCPUObjectData(),
                        mergedBuffer->getObjectCount(),
                        *mergedBuffer,
                        terrain.adapter->getCachedGPUTileData(),
                        terrain.pipeline->getCurrentTileCount());
                }
                else if (mergedBuffer->getObjectCount() > 0)
                {
                    accelStructManager->buildTLAS(cmd,
                        mergedBuffer->getCPUObjectData(),
                        mergedBuffer->getObjectCount(),
                        *mergedBuffer);
                }

                if (rtShadowProfiler && rtShadowProfiler->isValid())
                {
                    rtShadowProfiler->writeTimestamp(cmd, profilerFI,
                        raytracing::RTShadowTimestamp::AfterTLASBuild,
                        vk::PipelineStageFlagBits::eAllCommands);
                    rtShadowProfiler->markTLASBuilt();
                }
            }
        }

        cullPipeline->dispatch(cmd, stats.totalObjects);
        batchManager->insertBarriersAfterCompute(cmd);
        recordShadowPasses(cmd, hasMeshObjects, hasTerrainTiles);
        dispatchVolumetricFog(cmd);

        if (lightStreamManager)
        {
            lightStreamManager->updatePriorities(cachedCamera.position);
            lightStreamManager->applyBudget();
        }
    }

    void GPUDrivenRenderer::dispatchAsyncCompute(vk::CommandBuffer asyncCmd)
    {
        if (!initialized || !enabled) return;

        if (lightCullingPipeline && lightBufferManager)
        {
            lightCullingPipeline->dispatch(asyncCmd, cameraBuffer->getData().view,
                                           lightBufferManager->getPointLightCount(),
                                           lightBufferManager->getSpotLightCount());
        }

        if (vegetation.grassInitialized && vegetation.grassRenderingEnabled)
            dispatchGrassCompute(asyncCmd, vegetation.cachedVisibleTiles);

        dispatchGIProbeUpdate(asyncCmd);
    }

    bool GPUDrivenRenderer::isRTShadowReady() const
    {
        return rtShadowEnabled &&
               device.isRayQuerySupported() &&
               accelStructManager && accelStructManager->isTLASReady() &&
               depthPrepass && depthPrepass->isInitialized() &&
               lightBufferManager && lightBufferManager->getDirectionalLightCount() > 0;
    }

    void GPUDrivenRenderer::initAccelerationStructures()
    {
        if (accelStructManager || !device.isRayQuerySupported()) return;
        if (!depthPrepass || !depthPrepass->isInitialized()) return;
        if (!lightBufferManager || lightBufferManager->getDirectionalLightCount() == 0) return;

        ensureAccelerationStructureManager();
    }

    void GPUDrivenRenderer::ensureAccelerationStructureManager()
    {
        if (accelStructManager) return;
        if (!device.isRayQuerySupported()) return;

        accelStructManager = std::make_unique<raytracing::AccelerationStructureManager>(device);
        accelStructManager->init();

        if (!accelStructManager->isInitialized())
        {
            accelStructManager.reset();
            return;
        }

        if (mergedBuffer)
        {
            mergedBuffer->onSubmeshLOD0Ready = [this](const std::string& path, const std::string& name,
                                                       uint32_t idx, const SubmeshLocation& loc) {
                if (accelStructManager) accelStructManager->notifyMeshReady(path, name, idx, loc);
            };
            mergedBuffer->onSubmeshRemoved = [this](const std::string& path, const std::string& name,
                                                     uint32_t idx) {
                if (accelStructManager) accelStructManager->notifyMeshRemoved(path, name, idx);
            };

            // Retroactively notify about already-loaded submeshes
            for (const auto& loc : mergedBuffer->getAllSubmeshLocations())
            {
                if (loc.lodStates[0] == LODStreamState::Ready)
                {
                    accelStructManager->notifyMeshReady(loc.meshPath, loc.submeshName, loc.submeshIndex, loc);
                }
            }
        }

        if (terrain.adapter)
        {
            terrain.adapter->onTileLODReady = [this](const std::string& tileKey,
                                                      uint32_t vOff, uint32_t vCount,
                                                      uint32_t iOff, uint32_t iCount) {
                if (accelStructManager) accelStructManager->notifyTerrainTileReady(tileKey, vOff, vCount, iOff, iCount);
            };
            terrain.adapter->onTileRemoved = [this](const std::string& tileKey) {
                if (accelStructManager) accelStructManager->notifyTerrainTileRemoved(tileKey);
            };
        }
    }

    void GPUDrivenRenderer::dispatchRTShadow(vk::CommandBuffer cmd, uint32_t imageIndex)
    {
        if (!isRTShadowReady()) return;

        // Lazy init — create RT shadow pipeline and recreate mesh pipelines with set 13
        if (!rtShadowPipeline)
        {
            auto pipeline = std::make_unique<raytracing::RTShadowPipeline>(device);
            pipeline->init(
                depthPrepass->getWidth(), depthPrepass->getHeight(),
                accelStructManager->getTLASDescriptorLayout());

            if (!pipeline->isInitialized())
                return; // local destroyed; retry next frame

            rtShadowPipeline = std::move(pipeline);

            if (meshShaderPipeline && shadowSystem)
            {
                // Init denoiser
                rtShadowDenoiser = std::make_unique<raytracing::RTShadowDenoiser>(device);
                rtShadowDenoiser->init(depthPrepass->getWidth(), depthPrepass->getHeight());

                vk::DescriptorSetLayout giLayout = (giCascadeManager && giCascadeManager->getProbeStorage())
                    ? giCascadeManager->getProbeStorage()->getSamplingLayout() : nullptr;
                vk::DescriptorSetLayout causticLayout = (water.causticsResources && water.causticsResources->isInitialized())
                    ? water.causticsResources->getDescriptorSetLayout() : nullptr;

                // Use denoiser layout/descriptor for set 13 if available, else raw shadow
                vk::DescriptorSetLayout rtMaskLayout = (rtShadowDenoiser && rtShadowDenoiser->isInitialized())
                    ? rtShadowDenoiser->getDenoisedMaskSamplerLayout()
                    : rtShadowPipeline->getShadowMaskSamplerLayout();
                vk::DescriptorSet rtMaskDescSet = (rtShadowDenoiser && rtShadowDenoiser->isInitialized())
                    ? rtShadowDenoiser->getDenoisedMaskSamplerDescriptorSet()
                    : rtShadowPipeline->getShadowMaskSamplerDescriptorSet();

                MeshPipelineInitInfo pipelineInfo{
                    .iblLayout = cachedIBLLayout,
                    .bindlessTextureLayout = bindlessTextures->getDescriptorSetLayout(),
                    .boneMatrixLayout = boneMatrixManager->getDescriptorSetLayout(),
                    .lightDataLayout = lightBufferManager->getDescriptorSetLayout(),
                    .clusterGridLayout = clusterGridManager->getDescriptorSetLayout(),
                    .cullingOutputLayout = lightCullingPipeline->getDescriptorSetLayout(),
                    .shadowDataLayout = shadowSystem->getShadowDataLayout(),
                    .shadowTextureLayout = shadowSystem->getShadowTextureLayout(),
                    .giProbeDataLayout = giLayout,
                    .causticLayout = causticLayout,
                    .rtShadowMaskLayout = rtMaskLayout,
                    .renderPass = cachedRenderPass
                };

                meshShaderPipeline->recreate(pipelineInfo);
                meshShaderPipeline->updateRTShadowMaskDescriptor(rtMaskDescSet);
                if (giLayout && giCascadeManager)
                    meshShaderPipeline->updateGIProbeDescriptor(giCascadeManager->getProbeStorage()->getSamplingDescSet());
                if (causticLayout)
                    meshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());

                if (transparentMeshShaderPipeline)
                {
                    pipelineInfo.transparentMode = true;
                    transparentMeshShaderPipeline->recreate(pipelineInfo);
                    transparentMeshShaderPipeline->updateRTShadowMaskDescriptor(rtMaskDescSet);
                    if (giLayout && giCascadeManager)
                        transparentMeshShaderPipeline->updateGIProbeDescriptor(giCascadeManager->getProbeStorage()->getSamplingDescSet());
                    if (causticLayout)
                        transparentMeshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());
                    pipelineInfo.transparentMode = false;
                }

                if (wboitMeshShaderPipeline && cachedWBOITRenderPass)
                {
                    pipelineInfo.renderPass = cachedWBOITRenderPass;
                    pipelineInfo.wboitMode = true;
                    wboitMeshShaderPipeline->recreate(pipelineInfo);
                    wboitMeshShaderPipeline->updateRTShadowMaskDescriptor(rtMaskDescSet);
                    if (giLayout && giCascadeManager)
                        wboitMeshShaderPipeline->updateGIProbeDescriptor(giCascadeManager->getProbeStorage()->getSamplingDescSet());
                    if (causticLayout)
                        wboitMeshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());
                }

                // Terrain pipeline: set RT shadow mask layout and recreate
                if (terrain.pipeline)
                {
                    terrain.pipeline->setRTShadowMaskLayout(rtMaskLayout);
                    terrain.pipeline->updateRTShadowMaskDescriptor(rtMaskDescSet);
                    terrain.pipeline->recreate(
                        cachedIBLLayout,
                        bindlessTextures->getDescriptorSetLayout(),
                        terrain.pipeline->getCachedMeshletLayout(),
                        terrain.pipeline->getCachedVertexLayout(),
                        lightBufferManager->getDescriptorSetLayout(),
                        clusterGridManager->getDescriptorSetLayout(),
                        lightCullingPipeline->getDescriptorSetLayout(),
                        shadowSystem->getShadowDataLayout(),
                        shadowSystem->getShadowTextureLayout(),
                        cachedRenderPass);
                }
            }
        }

        if (!rtShadowPipeline->isInitialized()) return;

        // Handle resize
        uint32_t w = depthPrepass->getWidth();
        uint32_t h = depthPrepass->getHeight();
        if (w != 0 && h != 0)
        {
            if (rtShadowPipeline->resize(w, h) && rtShadowProfiler)
                rtShadowProfiler->invalidateFrameSlots();
            if (rtShadowDenoiser && rtShadowDenoiser->isInitialized())
            {
                rtShadowDenoiser->resize(w, h);
                // Re-bind denoised output after resize
                if (meshShaderPipeline)
                    meshShaderPipeline->updateRTShadowMaskDescriptor(rtShadowDenoiser->getDenoisedMaskSamplerDescriptorSet());
                if (transparentMeshShaderPipeline)
                    transparentMeshShaderPipeline->updateRTShadowMaskDescriptor(rtShadowDenoiser->getDenoisedMaskSamplerDescriptorSet());
                if (wboitMeshShaderPipeline)
                    wboitMeshShaderPipeline->updateRTShadowMaskDescriptor(rtShadowDenoiser->getDenoisedMaskSamplerDescriptorSet());
                if (terrain.pipeline)
                    terrain.pipeline->updateRTShadowMaskDescriptor(rtShadowDenoiser->getDenoisedMaskSamplerDescriptorSet());
            }
            else
            {
                // No denoiser: update mesh pipelines with raw shadow mask after resize
                vk::DescriptorSet rawMask = rtShadowPipeline->getShadowMaskSamplerDescriptorSet();
                if (meshShaderPipeline)
                    meshShaderPipeline->updateRTShadowMaskDescriptor(rawMask);
                if (transparentMeshShaderPipeline)
                    transparentMeshShaderPipeline->updateRTShadowMaskDescriptor(rawMask);
                if (wboitMeshShaderPipeline)
                    wboitMeshShaderPipeline->updateRTShadowMaskDescriptor(rawMask);
                if (terrain.pipeline)
                    terrain.pipeline->updateRTShadowMaskDescriptor(rawMask);
            }
        }

        // Skip RT shadow dispatch if TLAS isn't ready
        if (!accelStructManager || !accelStructManager->isTLASReady()) return;

        auto lightDir = lightBufferManager->getFirstDirectionalLightDirection();
        if (!lightDir.has_value()) return;

        const auto& camData = cameraBuffer->getData();
        bool useDenoiser = rtShadowDenoiser && rtShadowDenoiser->isInitialized();
        // Set runtime flag so fragment shader uses RT for directional shadows
        lightBufferManager->setRTShadowActive(useDenoiser);

        uint32_t fi = imageIndex % core::MAX_FRAMES_IN_FLIGHT;
        
        // Readback previous frame's profiling data and evaluate adaptive budget
        if (rtShadowProfiler && rtShadowProfiler->isValid())
        {
            rtShadowProfiler->readbackAndUpdate(device.getLogicalDevice(), fi,
                                                 accelStructManager->getMemoryBudget());
                                                 
            auto action = rtShadowProfiler->evaluateBudget();
            if (action.skipFrame) return;
            if (action.newMaxRayDistance.has_value())
                rtShadowPipeline->setMaxRayDistance(action.newMaxRayDistance.value());
            if (action.newSpatialPasses.has_value() && rtShadowDenoiser)
                rtShadowDenoiser->setSpatialPasses(action.newSpatialPasses.value());

            rtShadowProfiler->writeTimestamp(cmd, fi,
                raytracing::RTShadowTimestamp::BeforeRayDispatch,
                vk::PipelineStageFlagBits::eComputeShader);
        }

        rtShadowPipeline->dispatch(cmd,
            depthPrepass->getDepthImageView(),
            depthPrepass->getDepthImage(),
            depthPrepass->getNormalImageView(),
            depthPrepass->getNormalImage(),
            accelStructManager->getTLASDescriptorSet(),
            camData.invViewProjection,
            glm::vec3(camData.cameraPosition),
            camData.farPlane,
            lightDir.value(),
            w, h,
            useDenoiser, // skip final transitions when denoiser handles them
            fi);

        if (rtShadowProfiler && rtShadowProfiler->isValid())
        {
            rtShadowProfiler->writeTimestamp(cmd, fi,
                raytracing::RTShadowTimestamp::AfterRayDispatch,
                vk::PipelineStageFlagBits::eComputeShader);
        }

        if (useDenoiser)
        {
            rtShadowDenoiser->dispatch(cmd,
                rtShadowPipeline->getShadowMaskStorageView(),
                rtShadowPipeline->getShadowMaskImage(),
                depthPrepass->getDepthImageView(),
                depthPrepass->getDepthImage(),
                depthPrepass->getNormalImageView(),
                depthPrepass->getNormalImage(),
                camData.invViewProjection,
                camData.viewProjection,
                w, h,
                camData.frameIndex,
                fi);
        }

        if (rtShadowProfiler && rtShadowProfiler->isValid())
        {
            rtShadowProfiler->writeTimestamp(cmd, fi,
                raytracing::RTShadowTimestamp::AfterDenoiser,
                vk::PipelineStageFlagBits::eComputeShader);
        }
    }

    void GPUDrivenRenderer::applyRTShadowSettings(const types::RTShadowSettings& settings)
    {
        rtShadowEnabled = settings.enabled;
        if (rtShadowPipeline)
        {
            rtShadowPipeline->setMaxRayDistance(settings.maxRayDistance);
            rtShadowPipeline->setNormalBias(settings.normalBias);
            rtShadowPipeline->setRayTMin(settings.rayTMin);
        }
        if (rtShadowDenoiser)
        {
            rtShadowDenoiser->setTemporalBlend(settings.temporalBlend);
            rtShadowDenoiser->setDepthThreshold(settings.depthThreshold);
            rtShadowDenoiser->setNormalThreshold(settings.normalThreshold);
            rtShadowDenoiser->setSpatialPhiDepth(settings.spatialPhiDepth);
            rtShadowDenoiser->setSpatialPhiNormal(settings.spatialPhiNormal);
            rtShadowDenoiser->setSpatialPasses(settings.spatialPasses);
        }
        if (rtShadowProfiler)
        {
            rtShadowProfiler->setBaseSettings(settings.maxRayDistance, settings.spatialPasses);
            rtShadowProfiler->applyBudgetSettings(settings);
        }
    }

    types::RTShadowStats GPUDrivenRenderer::getRTShadowStats() const
    {
        if (!rtShadowProfiler) return {};
        raytracing::ASMemoryBudget asBudget;
        if (accelStructManager)
            asBudget = accelStructManager->getMemoryBudget();
        return rtShadowProfiler->getStats(asBudget);
    }
}
