#include "GPUDrivenRenderer.hpp"
#include "../occlusion/DepthPrepass.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Device.hpp"
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
                    accelStructManager = std::make_unique<raytracing::AccelerationStructureManager>(device);
                    accelStructManager->init();
                    if (accelStructManager->isInitialized())
                    {
                        tlasLayout = accelStructManager->getTLASDescriptorLayout();

                        // Wire mesh streaming callbacks to notify AS manager
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
                        }
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

    void GPUDrivenRenderer::dispatchGraphicsCompute(vk::CommandBuffer cmd)
    {
        if (!initialized || !enabled) return;

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
            if (accelStructManager->hasPendingBLASBuilds())
            {
                accelStructManager->buildPendingBLAS(cmd,
                    mergedBuffer->getVertexBuffer(), 64,
                    mergedBuffer->getIndexBuffer());
            }
            if (mergedBuffer->getObjectCount() > 0)
            {
                accelStructManager->buildTLAS(cmd,
                    mergedBuffer->getCPUObjectData(),
                    mergedBuffer->getObjectCount(),
                    *mergedBuffer);
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
        return device.isRayQuerySupported() &&
               accelStructManager && accelStructManager->isTLASReady() &&
               depthPrepass && depthPrepass->isInitialized() &&
               lightBufferManager && lightBufferManager->getDirectionalLightCount() > 0;
    }

    void GPUDrivenRenderer::dispatchRTShadow(vk::CommandBuffer cmd)
    {
        if (!isRTShadowReady()) return;

        // Lazy init — create RT shadow pipeline and recreate mesh pipelines with set 13
        if (!rtShadowPipeline)
        {
            rtShadowPipeline = std::make_unique<raytracing::RTShadowPipeline>(device);
            rtShadowPipeline->init(
                depthPrepass->getWidth(), depthPrepass->getHeight(),
                accelStructManager->getTLASDescriptorLayout());

            if (rtShadowPipeline->isInitialized() && meshShaderPipeline && shadowSystem)
            {
                vk::DescriptorSetLayout giLayout = (giCascadeManager && giCascadeManager->getProbeStorage())
                    ? giCascadeManager->getProbeStorage()->getSamplingLayout() : nullptr;
                vk::DescriptorSetLayout causticLayout = (water.causticsResources && water.causticsResources->isInitialized())
                    ? water.causticsResources->getDescriptorSetLayout() : nullptr;

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
                    .rtShadowMaskLayout = rtShadowPipeline->getShadowMaskSamplerLayout(),
                    .renderPass = cachedRenderPass
                };

                meshShaderPipeline->recreate(pipelineInfo);
                meshShaderPipeline->updateRTShadowMaskDescriptor(rtShadowPipeline->getShadowMaskSamplerDescriptorSet());
                if (giLayout && giCascadeManager)
                    meshShaderPipeline->updateGIProbeDescriptor(giCascadeManager->getProbeStorage()->getSamplingDescSet());
                if (causticLayout)
                    meshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());

                if (transparentMeshShaderPipeline)
                {
                    pipelineInfo.transparentMode = true;
                    transparentMeshShaderPipeline->recreate(pipelineInfo);
                    transparentMeshShaderPipeline->updateRTShadowMaskDescriptor(rtShadowPipeline->getShadowMaskSamplerDescriptorSet());
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
                    wboitMeshShaderPipeline->updateRTShadowMaskDescriptor(rtShadowPipeline->getShadowMaskSamplerDescriptorSet());
                    if (giLayout && giCascadeManager)
                        wboitMeshShaderPipeline->updateGIProbeDescriptor(giCascadeManager->getProbeStorage()->getSamplingDescSet());
                    if (causticLayout)
                        wboitMeshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());
                }
            }
        }

        if (!rtShadowPipeline->isInitialized()) return;

        // Handle resize
        if (depthPrepass->getWidth() != 0 && depthPrepass->getHeight() != 0)
        {
            rtShadowPipeline->resize(depthPrepass->getWidth(), depthPrepass->getHeight());
        }

        auto lightDir = lightBufferManager->getFirstDirectionalLightDirection();
        if (!lightDir.has_value()) return;

        const auto& camData = cameraBuffer->getData();

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
            500.0f,
            depthPrepass->getWidth(),
            depthPrepass->getHeight());
    }
}
