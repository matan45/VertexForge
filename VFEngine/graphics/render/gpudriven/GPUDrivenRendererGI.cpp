#include "GPUDrivenRenderer.hpp"
#include "SelectionMaskPipeline.hpp"
#include "../occlusion/DepthPrepass.hpp"
// VK-1443: full manager types (forward-declared in GPUDrivenRenderer.hpp) needed here
// because this TU dispatches GI probe + RT shadow work and defines upsampleLayeredRTShadow.
#include "../gi/ProbeTracePipeline.hpp"
#include "../gi/ProbeUpdatePipeline.hpp"
#include "../raytracing/AccelerationStructureManager.hpp"
#include "../raytracing/RTShadowPipeline.hpp"
#include "../raytracing/RTShadowDenoiser.hpp"
#include "../raytracing/RTShadowProfiler.hpp"
#include "../raytracing/RTLayeredShadowPipeline.hpp"
#include "../raytracing/RTLayeredShadowDenoiser.hpp"
#include "../raytracing/RTShadowUpsamplePipeline.hpp"
#include "../raytracing/RTLayeredShadowUpsamplePipeline.hpp"
#include "../upscaling/UpscaleManager.hpp"
#include "../custom/PluginTextureManager.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Device.hpp"
#include "../../core/RenderManager.hpp"
#include "../../core/GraphicsConstants.hpp"
#include "../../core/ImageUtilities.hpp"
#include "types/RenderSettings.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <thread>

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::gpudriven
{
    namespace
    {
        // VK-1431: the full-res RT-shadow mask producer the fragment shader samples, selected
        // identically for layout AND descriptor set so the two can never desync (a set-13/15/16
        // layout-vs-set mismatch is a Vulkan validation error at draw). Precedence: half-mode
        // upsample > denoiser > raw pipeline > none. All three pipeline families (directional
        // RTShadow* and layered RTLayered*) expose the same getter names, so one template binds all.
        struct RTMaskBinding
        {
            vk::DescriptorSetLayout layout = nullptr;
            vk::DescriptorSet set = nullptr;
        };

        template <class Pipe, class Den, class Up>
        RTMaskBinding selectActiveRTMask(bool half, const Pipe* pipe, const Den* den, const Up* up)
        {
            if (!pipe || !pipe->isInitialized())
                return {};
            if (half && up && up->isInitialized())
                return {up->getOutputSamplerLayout(), up->getOutputSamplerDescriptorSet()};
            if (den && den->isInitialized())
                return {den->getDenoisedMaskSamplerLayout(), den->getDenoisedMaskSamplerDescriptorSet()};
            return {pipe->getShadowMaskSamplerLayout(), pipe->getShadowMaskSamplerDescriptorSet()};
        }

        // VK-1431: the depth/normal SHADER_READ transition + compute->compute mask barrier + restore
        // dance shared by the directional and layered Half-mode upsample paths. The ordering
        // (transitions -> compute->compute barrier -> dispatch -> restore) is fixed here so every
        // upsample site is byte-identical; the only per-site difference is the dispatch callable.
        template <class DispatchFn>
        void withUpsampleGuides(vk::CommandBuffer cmd, occlusion::DepthPrepass& depthPrepass,
                                DispatchFn&& doDispatch)
        {
            // The denoiser left depth in attachment + normal in color-attachment layout; transition
            // both to SHADER_READ for the upsample guide reads, then restore so downstream passes see
            // the legacy (attachment) state.
            core::ImageUtilities::transitionImageLayout(cmd, depthPrepass.getDepthImage(),
                vk::ImageLayout::eDepthStencilAttachmentOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eDepth);
            core::ImageUtilities::transitionImageLayout(cmd, depthPrepass.getNormalImage(),
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);

            // VK-1430: make the denoiser's compute write to the denoised mask visible to the upsample's
            // compute read. The denoiser's final transition only scoped that write to the FRAGMENT
            // stage (eGeneral->eShaderReadOnlyOptimal); the upsample reads it in COMPUTE, so without
            // this compute->compute dependency it is a RAW hazard. The image is already in
            // eShaderReadOnlyOptimal, so a plain memory barrier (no layout change) suffices.
            {
                vk::MemoryBarrier maskBarrier{};
                maskBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
                maskBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
                cmd.pipelineBarrier(
                    vk::PipelineStageFlagBits::eComputeShader,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, 1, &maskBarrier, 0, nullptr, 0, nullptr);
            }

            doDispatch();

            core::ImageUtilities::transitionImageLayout(cmd, depthPrepass.getDepthImage(),
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageLayout::eDepthStencilAttachmentOptimal,
                vk::ImageAspectFlagBits::eDepth);
            core::ImageUtilities::transitionImageLayout(cmd, depthPrepass.getNormalImage(),
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);
        }
    }

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

            vk::MemoryBarrier2 initBarrier{};
            initBarrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            initBarrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            initBarrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
            initBarrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;
            vk::DependencyInfo depInfo{};
            depInfo.memoryBarrierCount = 1;
            depInfo.pMemoryBarriers = &initBarrier;
            cmd.pipelineBarrier2KHR(depInfo);
        }

        auto batches = giCascadeManager->getProbeUpdateBatches();
        if (!storage || batches.empty()) return;

        // No compute-to-compute barrier needed: batches update disjoint probe regions
        // with no cross-batch dependencies.

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

        vk::MemoryBarrier2 computeBarrier{};
        computeBarrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        computeBarrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
        computeBarrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
        computeBarrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
        vk::DependencyInfo computeDepInfo{};
        computeDepInfo.memoryBarrierCount = 1;
        computeDepInfo.pMemoryBarriers = &computeBarrier;
        cmd.pipelineBarrier2KHR(computeDepInfo);

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

        vk::MemoryBarrier2 copyBarrier{};
        copyBarrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        copyBarrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        copyBarrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader;
        copyBarrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
        vk::DependencyInfo copyDepInfo{};
        copyDepInfo.memoryBarrierCount = 1;
        copyDepInfo.pMemoryBarriers = &copyBarrier;
        cmd.pipelineBarrier2KHR(copyDepInfo);

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

                if (!cachedColorFormats.empty())
                {
                    giDebugRenderer = std::make_unique<gi::GIDebugRenderer>(device);
                    giDebugRenderer->init(cachedColorFormats[0], cachedDepthFormat, storage->getProbeDataLayout(), storage->getCascadeInfoLayout());
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
                        .worldMaskLayout = currentWorldMaskLayout(),
                .selectionCoverageLayout = selectionMaskPipeline ? selectionMaskPipeline->getDescriptorSetLayout() : vk::DescriptorSetLayout{},
                .colorAttachmentFormats = cachedColorFormats,
                        .depthAttachmentFormat = cachedDepthFormat
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

                    if (wboitMeshShaderPipeline && !cachedWBOITColorFormats.empty())
                    {
                        pipelineInfo.colorAttachmentFormats = cachedWBOITColorFormats;
                        pipelineInfo.depthAttachmentFormat = cachedWBOITDepthFormat;
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
        if (rtSpotShadowDenoiser) { rtSpotShadowDenoiser->cleanup(); rtSpotShadowDenoiser.reset(); }
        if (rtSpotShadowPipeline) { rtSpotShadowPipeline->cleanup(); rtSpotShadowPipeline.reset(); }
        if (rtPointShadowDenoiser) { rtPointShadowDenoiser->cleanup(); rtPointShadowDenoiser.reset(); }
        if (rtPointShadowPipeline) { rtPointShadowPipeline->cleanup(); rtPointShadowPipeline.reset(); }
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

            if (!giCascadeManager && meshShaderPipeline && shadowSystem && !cachedColorFormats.empty())
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
                    .worldMaskLayout = currentWorldMaskLayout(),
                    .selectionCoverageLayout = selectionMaskPipeline ? selectionMaskPipeline->getDescriptorSetLayout() : vk::DescriptorSetLayout{},
                    .colorAttachmentFormats = cachedColorFormats,
                    .depthAttachmentFormat = cachedDepthFormat
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
                if (wboitMeshShaderPipeline && !cachedWBOITColorFormats.empty())
                {
                    pipelineInfo.colorAttachmentFormats = cachedWBOITColorFormats;
                    pipelineInfo.depthAttachmentFormat = cachedWBOITDepthFormat;
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

    void GPUDrivenRenderer::buildAccelerationStructures(vk::CommandBuffer cmd, uint32_t imageIndex)
    {
        // A7: build/update acceleration structures for RT shadows and GI. Only do the (expensive)
        // BLAS/per-frame-TLAS work when something actually consumes the structure: RT shadows
        // enabled, or GI (radiance cascades) active. Otherwise skip it entirely even if a
        // manager object lingers (e.g. created during the pre-settings window where
        // rtShadowEnabled defaults true, or left over after RT was toggled off).
        // Must run AFTER the transfer->compute barrier (both dispatch paths satisfy this).
        bool accelStructNeeded = rtShadowEnabled || rtSpotShadowEnabled || rtPointShadowEnabled || (giCascadeManager != nullptr);
        if (accelStructNeeded && accelStructManager && accelStructManager->isInitialized() && mergedBuffer)
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

            // Drain queued BLAS compactions before the TLAS build so compacted device
            // addresses (and the forced-rebuild flag) are consumed this same frame (VK-1430).
            accelStructManager->processPendingCompactions(cmd);

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
    }

    void GPUDrivenRenderer::dispatchGraphicsCompute(vk::CommandBuffer cmd, uint32_t imageIndex)
    {
        // VK-1398: record the image index of this submission for per-image RT-mask ring binding.
        currentImageIndex = imageIndex;
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

        vk::MemoryBarrier memBarrier{vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                            vk::DependencyFlags{}, 1, &memBarrier, 0, nullptr, 0, nullptr);

        buildAccelerationStructures(cmd, imageIndex);

        // VK-1334: pick per-RTT cull descriptor when this thread is recording an RTT pre-pass.
        // Defaults to nullptr on the main render thread, so main keeps using the cull pipeline's
        // internal (main-camera) descriptor.
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

    // VK-1431: all six getActive*RTShadowMask{Layout,DescriptorSet} accessors share the same Half-mode
    // producer ladder (upsample > denoiser > raw). selectActiveRTMask picks the producer once and
    // returns both its layout and set, so the layout/set pair is always in lockstep by construction.
    vk::DescriptorSetLayout GPUDrivenRenderer::getActiveRTShadowMaskLayout() const
    {
        return selectActiveRTMask(rtShadowHalfResolution, rtShadowPipeline.get(),
                                  rtShadowDenoiser.get(), rtShadowUpsample.get()).layout;
    }

    vk::DescriptorSet GPUDrivenRenderer::getActiveRTShadowMaskDescriptorSet() const
    {
        return selectActiveRTMask(rtShadowHalfResolution, rtShadowPipeline.get(),
                                  rtShadowDenoiser.get(), rtShadowUpsample.get()).set;
    }

    vk::DescriptorSetLayout GPUDrivenRenderer::getActiveRTSpotShadowMaskLayout() const
    {
        return selectActiveRTMask(rtShadowHalfResolution, rtSpotShadowPipeline.get(),
                                  rtSpotShadowDenoiser.get(), rtSpotShadowUpsample.get()).layout;
    }

    vk::DescriptorSet GPUDrivenRenderer::getActiveRTSpotShadowMaskDescriptorSet() const
    {
        return selectActiveRTMask(rtShadowHalfResolution, rtSpotShadowPipeline.get(),
                                  rtSpotShadowDenoiser.get(), rtSpotShadowUpsample.get()).set;
    }

    bool GPUDrivenRenderer::hasBoundDescriptorSetCapacity(uint32_t requiredSetCount) const
    {
        // The device limit is fixed for the process lifetime; query + warn once.
        static const uint32_t maxBoundSets =
            device.getPhysicalDevice().getProperties().limits.maxBoundDescriptorSets;
        if (maxBoundSets >= requiredSetCount)
            return true;
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            vfLogWarning("RT shadows need {} bound descriptor sets but this device supports only "
                         "{} (maxBoundDescriptorSets); RT spot/point shadows disabled on this GPU.",
                         requiredSetCount, maxBoundSets);
        }
        return false;
    }

    bool GPUDrivenRenderer::isRTSpotShadowReady() const
    {
        return rtSpotShadowEnabled &&
               device.isRayQuerySupported() &&
               hasBoundDescriptorSetCapacity(15) && // RT mask is set 13, but world mask may occupy set 14 (GI on) -> need 15 sets
               accelStructManager && accelStructManager->isTLASReady() &&
               depthPrepass && depthPrepass->isInitialized() &&
               lightBufferManager && lightBufferManager->getSpotLightCount() > 0;
    }

    vk::DescriptorSetLayout GPUDrivenRenderer::getActiveRTPointShadowMaskLayout() const
    {
        return selectActiveRTMask(rtShadowHalfResolution, rtPointShadowPipeline.get(),
                                  rtPointShadowDenoiser.get(), rtPointShadowUpsample.get()).layout;
    }

    vk::DescriptorSet GPUDrivenRenderer::getActiveRTPointShadowMaskDescriptorSet() const
    {
        return selectActiveRTMask(rtShadowHalfResolution, rtPointShadowPipeline.get(),
                                  rtPointShadowDenoiser.get(), rtPointShadowUpsample.get()).set;
    }

    void GPUDrivenRenderer::upsampleLayeredRTShadow(vk::CommandBuffer cmd,
                                                    raytracing::RTLayeredShadowUpsamplePipeline& upsample,
                                                    const raytracing::RTLayeredShadowDenoiser& denoiser,
                                                    const std::vector<raytracing::RTLayeredDispatchInfo>& slices,
                                                    uint32_t traceW, uint32_t traceH,
                                                    uint32_t fullW, uint32_t fullH,
                                                    uint32_t frameIndex)
    {
        if (slices.empty()) return;

        std::vector<raytracing::RTLayeredUpsampleInfo> upsampleList;
        upsampleList.reserve(slices.size());
        for (const auto& d : slices)
        {
            raytracing::RTLayeredUpsampleInfo ui{};
            ui.slice = d.slice;
            ui.halfResDenoisedLayerView = denoiser.getDenoisedMaskLayerView(d.slice);
            upsampleList.push_back(ui);
        }

        // VK-1431: the depth/normal SHADER_READ transition + compute->compute mask barrier + restore
        // dance is shared with the directional path via withUpsampleGuides; only the dispatch differs.
        withUpsampleGuides(cmd, *depthPrepass, [&] {
            upsample.dispatch(cmd, upsampleList,
                depthPrepass->getDepthImageView(), depthPrepass->getNormalImageView(),
                traceW, traceH, fullW, fullH,
                rtShadowUpsampleDepthThreshold, rtShadowUpsampleNormalExp,
                frameIndex);
        });
    }

    bool GPUDrivenRenderer::isRTPointShadowReady() const
    {
        return rtPointShadowEnabled &&
               device.isRayQuerySupported() &&
               hasBoundDescriptorSetCapacity(15) && // RT mask is set 13, but world mask may occupy set 14 (GI on) -> need 15 sets
               accelStructManager && accelStructManager->isTLASReady() &&
               depthPrepass && depthPrepass->isInitialized() &&
               lightBufferManager && lightBufferManager->getPointLightCount() > 0;
    }

    void GPUDrivenRenderer::initAccelerationStructures()
    {
        // Only RT shadows drive this lazy-init path. GI creates the acceleration structure
        // through its own init (ensureAccelerationStructureManager in initGI). Without this
        // gate the BLAS/TLAS (incl. a BLAS per terrain tile + a per-frame TLAS rebuild) were
        // built on any ray-query-capable GPU with a sun, even with RT shadows AND GI off.
        // RT directional needs a sun; RT spot needs at least one spot light. Either drives the build.
        const bool directionalWantsRT = rtShadowEnabled;
        const bool spotWantsRT = rtSpotShadowEnabled && lightBufferManager &&
                                 lightBufferManager->getSpotLightCount() > 0;
        const bool pointWantsRT = rtPointShadowEnabled && lightBufferManager &&
                                  lightBufferManager->getPointLightCount() > 0;
        if (!directionalWantsRT && !spotWantsRT && !pointWantsRT) return;
        if (accelStructManager || !device.isRayQuerySupported()) return;
        if (!depthPrepass || !depthPrepass->isInitialized()) return;
        if (!lightBufferManager) return;
        const bool hasDirectional = lightBufferManager->getDirectionalLightCount() > 0;
        const bool hasSpot = lightBufferManager->getSpotLightCount() > 0;
        const bool hasPoint = lightBufferManager->getPointLightCount() > 0;
        if (!(directionalWantsRT && hasDirectional) && !(spotWantsRT && hasSpot) &&
            !(pointWantsRT && hasPoint)) return;

        ensureAccelerationStructureManager();
    }

    void GPUDrivenRenderer::ensureAccelerationStructureManager()
    {
        if (accelStructManager) return;
        if (!device.isRayQuerySupported()) return;

        accelStructManager = std::make_unique<raytracing::AccelerationStructureManager>(device);
        accelStructManager->init();
        if (core::RenderManager::getGlobalDeletionQueue())
            accelStructManager->setDeletionQueue(core::RenderManager::getGlobalDeletionQueue());

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

    vk::DescriptorSetLayout GPUDrivenRenderer::currentWorldMaskLayout() const
    {
        return (pluginTextureManager && pluginTextureManager->hasMaskResources())
                   ? pluginTextureManager->getEntityMaskLayout()
                   : vk::DescriptorSetLayout{};
    }

    void GPUDrivenRenderer::dispatchWorldMask()
    {
        if (!initialized || !pluginTextureManager || !pluginTextureManager->hasMaskResources())
            return;

        if (!meshShaderPipeline || !shadowSystem || !bindlessTextures || !boneMatrixManager ||
            !lightBufferManager || !clusterGridManager || !lightCullingPipeline)
            return;

        // One-time pipeline recreate with WORLD_MASK_ENABLED on the first bind.
        if (pluginTextureManager->consumeNeedsPipelineRecreate())
        {
            recreateScenePipelinesForWorldMask();
        }

        // Keep descriptor pointers fresh — cheap stores; the sets are owned by the manager
        // and survive any pipeline recreate (GI/water/motion-vector toggles).
        vk::DescriptorSet maskSet = pluginTextureManager->getEntityMaskDescriptorSet();
        meshShaderPipeline->updateWorldMaskDescriptor(maskSet);
        if (transparentMeshShaderPipeline)
            transparentMeshShaderPipeline->updateWorldMaskDescriptor(maskSet);
        if (wboitMeshShaderPipeline)
            wboitMeshShaderPipeline->updateWorldMaskDescriptor(maskSet);

        // Terrain samples via its own set-11 bindings 3/4 — rewrite when the bound texture changes.
        if (terrain.pipeline && pluginTextureManager->getMaskDescriptorVersion() != lastWorldMaskVersion)
        {
            lastWorldMaskVersion = pluginTextureManager->getMaskDescriptorVersion();
            terrain.pipeline->updateWorldMaskResources(
                pluginTextureManager->getMaskImageView(),
                pluginTextureManager->getMaskSampler(),
                pluginTextureManager->getMaskParamsBuffer(),
                custom::PluginTextureManager::getMaskParamsSize());
        }
    }

    void GPUDrivenRenderer::recreateScenePipelinesForWorldMask()
    {
        vk::DescriptorSetLayout giLayout = (giCascadeManager && giCascadeManager->getProbeStorage())
            ? giCascadeManager->getProbeStorage()->getSamplingLayout() : vk::DescriptorSetLayout{};
        vk::DescriptorSetLayout causticLayout = (water.causticsResources && water.causticsResources->isInitialized())
            ? water.causticsResources->getDescriptorSetLayout() : vk::DescriptorSetLayout{};

        // Preserve the RT shadow mask (set 13) if RT shadows are already active.
        vk::DescriptorSetLayout rtMaskLayout{};
        vk::DescriptorSet rtMaskDescSet{};
        if (rtShadowPipeline && rtShadowPipeline->isInitialized())
        {
            const bool useDenoised = rtShadowDenoiser && rtShadowDenoiser->isInitialized();
            rtMaskLayout = useDenoised ? rtShadowDenoiser->getDenoisedMaskSamplerLayout()
                                       : rtShadowPipeline->getShadowMaskSamplerLayout();
            rtMaskDescSet = useDenoised ? rtShadowDenoiser->getDenoisedMaskSamplerDescriptorSet()
                                        : rtShadowPipeline->getShadowMaskSamplerDescriptorSet();
        }

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
            .worldMaskLayout = pluginTextureManager->getEntityMaskLayout(),
            .rtSpotShadowMaskLayout = getActiveRTSpotShadowMaskLayout(),
            .rtPointShadowMaskLayout = getActiveRTPointShadowMaskLayout(),
            .selectionCoverageLayout = selectionMaskPipeline ? selectionMaskPipeline->getDescriptorSetLayout() : vk::DescriptorSetLayout{},
            .colorAttachmentFormats = cachedColorFormats,
            .depthAttachmentFormat = cachedDepthFormat
        };

        vk::DescriptorSet spotMaskDescSet = getActiveRTSpotShadowMaskDescriptorSet();
        vk::DescriptorSet pointMaskDescSet = getActiveRTPointShadowMaskDescriptorSet();

        auto restoreDescriptors = [&](MeshShaderPipeline& pipeline)
        {
            if (rtMaskDescSet) pipeline.updateRTShadowMaskDescriptor(rtMaskDescSet);
            if (spotMaskDescSet) pipeline.updateRTSpotShadowMaskDescriptor(spotMaskDescSet);
            if (pointMaskDescSet) pipeline.updateRTPointShadowMaskDescriptor(pointMaskDescSet);
            if (giLayout && giCascadeManager)
                pipeline.updateGIProbeDescriptor(giCascadeManager->getProbeStorage()->getSamplingDescSet());
            if (causticLayout)
                pipeline.updateCausticDescriptor(water.causticsResources->getDescriptorSet());
            pipeline.updateWorldMaskDescriptor(pluginTextureManager->getEntityMaskDescriptorSet());
        };

        meshShaderPipeline->recreate(pipelineInfo);
        restoreDescriptors(*meshShaderPipeline);

        if (transparentMeshShaderPipeline)
        {
            pipelineInfo.transparentMode = true;
            transparentMeshShaderPipeline->recreate(pipelineInfo);
            restoreDescriptors(*transparentMeshShaderPipeline);
            pipelineInfo.transparentMode = false;
        }

        if (wboitMeshShaderPipeline && !cachedWBOITColorFormats.empty())
        {
            pipelineInfo.colorAttachmentFormats = cachedWBOITColorFormats;
            pipelineInfo.depthAttachmentFormat = cachedWBOITDepthFormat;
            pipelineInfo.wboitMode = true;
            wboitMeshShaderPipeline->recreate(pipelineInfo);
            restoreDescriptors(*wboitMeshShaderPipeline);
        }

        // Terrain: bindings 3/4 already live in terrainDataLayout — only the macro needs
        // the pipeline rebuilt so the shader compiles the sampling branch.
        if (terrain.pipeline)
        {
            terrain.pipeline->setWorldMaskEnabled(true);
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
                cachedColorFormats, cachedDepthFormat);
        }

        vfLogInfo("GPUDrivenRenderer: world mask bound — scene + terrain pipelines recreated with WORLD_MASK_ENABLED");
    }

    void GPUDrivenRenderer::recreateScenePipelinesForSVT()
    {
        // VK-1209: rebuild only the scene MESH pipelines (not terrain) from current state so the
        // set-1 SVT bindings + SVT_ENABLED match each pipeline's svtSampleEnabled flag. Mirrors
        // recreateScenePipelinesForWorldMask but pluginTextureManager-guarded and terrain-free.
        if (!meshShaderPipeline)
            return;

        vk::DescriptorSetLayout giLayout = (giCascadeManager && giCascadeManager->getProbeStorage())
            ? giCascadeManager->getProbeStorage()->getSamplingLayout() : vk::DescriptorSetLayout{};
        vk::DescriptorSetLayout causticLayout = (water.causticsResources && water.causticsResources->isInitialized())
            ? water.causticsResources->getDescriptorSetLayout() : vk::DescriptorSetLayout{};

        vk::DescriptorSetLayout rtMaskLayout{};
        vk::DescriptorSet rtMaskDescSet{};
        if (rtShadowPipeline && rtShadowPipeline->isInitialized())
        {
            const bool useDenoised = rtShadowDenoiser && rtShadowDenoiser->isInitialized();
            rtMaskLayout = useDenoised ? rtShadowDenoiser->getDenoisedMaskSamplerLayout()
                                       : rtShadowPipeline->getShadowMaskSamplerLayout();
            rtMaskDescSet = useDenoised ? rtShadowDenoiser->getDenoisedMaskSamplerDescriptorSet()
                                        : rtShadowPipeline->getShadowMaskSamplerDescriptorSet();
        }

        vk::DescriptorSetLayout worldMaskLayout = pluginTextureManager
            ? pluginTextureManager->getEntityMaskLayout() : vk::DescriptorSetLayout{};

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
            .worldMaskLayout = worldMaskLayout,
            .rtSpotShadowMaskLayout = getActiveRTSpotShadowMaskLayout(),
            .rtPointShadowMaskLayout = getActiveRTPointShadowMaskLayout(),
            .selectionCoverageLayout = selectionMaskPipeline ? selectionMaskPipeline->getDescriptorSetLayout() : vk::DescriptorSetLayout{},
            .colorAttachmentFormats = cachedColorFormats,
            .depthAttachmentFormat = cachedDepthFormat
        };

        vk::DescriptorSet spotMaskDescSet = getActiveRTSpotShadowMaskDescriptorSet();
        vk::DescriptorSet pointMaskDescSet = getActiveRTPointShadowMaskDescriptorSet();

        auto restoreDescriptors = [&](MeshShaderPipeline& pipeline)
        {
            if (rtMaskDescSet) pipeline.updateRTShadowMaskDescriptor(rtMaskDescSet);
            if (spotMaskDescSet) pipeline.updateRTSpotShadowMaskDescriptor(spotMaskDescSet);
            if (pointMaskDescSet) pipeline.updateRTPointShadowMaskDescriptor(pointMaskDescSet);
            if (giLayout && giCascadeManager)
                pipeline.updateGIProbeDescriptor(giCascadeManager->getProbeStorage()->getSamplingDescSet());
            if (causticLayout)
                pipeline.updateCausticDescriptor(water.causticsResources->getDescriptorSet());
            if (worldMaskLayout && pluginTextureManager)
                pipeline.updateWorldMaskDescriptor(pluginTextureManager->getEntityMaskDescriptorSet());
        };

        meshShaderPipeline->recreate(pipelineInfo);
        restoreDescriptors(*meshShaderPipeline);

        if (transparentMeshShaderPipeline)
        {
            pipelineInfo.transparentMode = true;
            transparentMeshShaderPipeline->recreate(pipelineInfo);
            restoreDescriptors(*transparentMeshShaderPipeline);
            pipelineInfo.transparentMode = false;
        }

        if (wboitMeshShaderPipeline && !cachedWBOITColorFormats.empty())
        {
            pipelineInfo.colorAttachmentFormats = cachedWBOITColorFormats;
            pipelineInfo.depthAttachmentFormat = cachedWBOITDepthFormat;
            pipelineInfo.wboitMode = true;
            wboitMeshShaderPipeline->recreate(pipelineInfo);
            restoreDescriptors(*wboitMeshShaderPipeline);
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
                    .worldMaskLayout = currentWorldMaskLayout(),
                    .rtSpotShadowMaskLayout = getActiveRTSpotShadowMaskLayout(),
                    .rtPointShadowMaskLayout = getActiveRTPointShadowMaskLayout(),
                    .selectionCoverageLayout = selectionMaskPipeline ? selectionMaskPipeline->getDescriptorSetLayout() : vk::DescriptorSetLayout{},
                    .colorAttachmentFormats = cachedColorFormats,
                    .depthAttachmentFormat = cachedDepthFormat
                };

                vk::DescriptorSet spotMaskDescSet = getActiveRTSpotShadowMaskDescriptorSet();
                vk::DescriptorSet pointMaskDescSet = getActiveRTPointShadowMaskDescriptorSet();

                meshShaderPipeline->recreate(pipelineInfo);
                meshShaderPipeline->updateRTShadowMaskDescriptor(rtMaskDescSet);
                if (spotMaskDescSet) meshShaderPipeline->updateRTSpotShadowMaskDescriptor(spotMaskDescSet);
                if (pointMaskDescSet) meshShaderPipeline->updateRTPointShadowMaskDescriptor(pointMaskDescSet);
                if (giLayout && giCascadeManager)
                    meshShaderPipeline->updateGIProbeDescriptor(giCascadeManager->getProbeStorage()->getSamplingDescSet());
                if (causticLayout)
                    meshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());

                if (transparentMeshShaderPipeline)
                {
                    pipelineInfo.transparentMode = true;
                    transparentMeshShaderPipeline->recreate(pipelineInfo);
                    transparentMeshShaderPipeline->updateRTShadowMaskDescriptor(rtMaskDescSet);
                    if (spotMaskDescSet) transparentMeshShaderPipeline->updateRTSpotShadowMaskDescriptor(spotMaskDescSet);
                    if (pointMaskDescSet) transparentMeshShaderPipeline->updateRTPointShadowMaskDescriptor(pointMaskDescSet);
                    if (giLayout && giCascadeManager)
                        transparentMeshShaderPipeline->updateGIProbeDescriptor(giCascadeManager->getProbeStorage()->getSamplingDescSet());
                    if (causticLayout)
                        transparentMeshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());
                    pipelineInfo.transparentMode = false;
                }

                if (wboitMeshShaderPipeline && !cachedWBOITColorFormats.empty())
                {
                    pipelineInfo.colorAttachmentFormats = cachedWBOITColorFormats;
                    pipelineInfo.depthAttachmentFormat = cachedWBOITDepthFormat;
                    pipelineInfo.wboitMode = true;
                    wboitMeshShaderPipeline->recreate(pipelineInfo);
                    wboitMeshShaderPipeline->updateRTShadowMaskDescriptor(rtMaskDescSet);
                    if (spotMaskDescSet) wboitMeshShaderPipeline->updateRTSpotShadowMaskDescriptor(spotMaskDescSet);
                    if (pointMaskDescSet) wboitMeshShaderPipeline->updateRTPointShadowMaskDescriptor(pointMaskDescSet);
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
                    if (getActiveRTSpotShadowMaskLayout())
                    {
                        terrain.pipeline->setRTSpotShadowMaskLayout(getActiveRTSpotShadowMaskLayout());
                        terrain.pipeline->updateRTSpotShadowMaskDescriptor(getActiveRTSpotShadowMaskDescriptorSet());
                    }
                    if (getActiveRTPointShadowMaskLayout())
                    {
                        terrain.pipeline->setRTPointShadowMaskLayout(getActiveRTPointShadowMaskLayout());
                        terrain.pipeline->updateRTPointShadowMaskDescriptor(getActiveRTPointShadowMaskDescriptorSet());
                    }
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
                        cachedColorFormats, cachedDepthFormat);
                }
            }
        }

        if (!rtShadowPipeline->isInitialized()) return;

        // Handle resize
        uint32_t w = depthPrepass->getWidth();
        uint32_t h = depthPrepass->getHeight();
        if (w != 0 && h != 0)
        {
            // VK-1430: trace + denoiser are sized to the (possibly half) trace dims; the upsample
            // output (Half only) is sized to full res. resize() early-outs when unchanged, so a
            // Full->Half toggle (which flips rtShadowHalfResolution in applyRTShadowSettings) auto-
            // reallocates everything to the right dims on the next frame.
            auto [tw, th] = rtShadowTraceDims(w, h);
            if (rtShadowPipeline->resize(tw, th) && rtShadowProfiler)
                rtShadowProfiler->invalidateFrameSlots();
            if (rtShadowDenoiser && rtShadowDenoiser->isInitialized())
                rtShadowDenoiser->resize(tw, th);
            // Lazily create the upsample the first time we run a Half frame with the pipeline online
            // (covers scenes that load with Half already set, before any Full->Half toggle).
            if (rtShadowHalfResolution && !rtShadowUpsample)
                rtShadowUpsample = std::make_unique<raytracing::RTShadowUpsamplePipeline>(device);
            if (rtShadowHalfResolution && rtShadowUpsample)
                rtShadowUpsample->resize(w, h);

            // Re-point the set-13 producer after any resize. getActiveRTShadowMaskDescriptorSet()
            // already selects upsample (Half) vs denoiser vs raw, so this one call covers all modes.
            vk::DescriptorSet activeMask = getActiveRTShadowMaskDescriptorSet();
            if (activeMask)
            {
                if (meshShaderPipeline) meshShaderPipeline->updateRTShadowMaskDescriptor(activeMask);
                if (transparentMeshShaderPipeline) transparentMeshShaderPipeline->updateRTShadowMaskDescriptor(activeMask);
                if (wboitMeshShaderPipeline) wboitMeshShaderPipeline->updateRTShadowMaskDescriptor(activeMask);
                if (terrain.pipeline) terrain.pipeline->updateRTShadowMaskDescriptor(activeMask);
            }
        }

        // Skip RT shadow dispatch if TLAS isn't ready. No fresh directional mask is produced this
        // frame, so clear the runtime flag: the directional VSM clipmap must render next frame
        // instead of being overridden by a stale RT mask. The override is correct ONLY while RT
        // actually produces a mask (TLAS rebuilds while geometry streams). (review fix)
        if (!accelStructManager || !accelStructManager->isTLASReady())
        {
            lightBufferManager->setRTShadowActive(false);
            return;
        }

        auto lightDir = lightBufferManager->getFirstDirectionalLightDirection();
        if (!lightDir.has_value())
        {
            // No directional light this frame → no directional RT shadows. Clear the flag so the
            // clipmap is not skipped against a stale mask. (review fix)
            lightBufferManager->setRTShadowActive(false);
            return;
        }

        const auto& camData = cameraBuffer->getData();
        bool useDenoiser = rtShadowDenoiser && rtShadowDenoiser->isInitialized();
        // When DLSS-D Ray Reconstruction is the active upscaler it denoises the shadows itself,
        // so skip the engine's traditional RTShadowDenoiser and let the noisy mask flow through
        // (the fragment shader then samples the raw RT mask). The user can force the traditional
        // denoiser back on for A/B comparison. RT shadows still dispatch either way. (VK-1245)
        if (render::upscaling::UpscaleManager::shouldBypassShadowDenoiser())
            useDenoiser = false;

        // The resize block above (which runs every frame) re-points the mesh pipelines to the
        // denoised mask whenever the denoiser is initialized. When RR bypass makes useDenoiser
        // false this frame, override that with the RAW shadow mask so the noisy shadows flow to
        // Ray Reconstruction. No-op when RR is off (useDenoiser == denoiser-initialized). (VK-1245)
        if (!useDenoiser && rtShadowDenoiser && rtShadowDenoiser->isInitialized())
        {
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

        // Set runtime flag so fragment shader uses RT for directional shadows (whether the
        // mask is denoised or raw — RR feeds the raw mask through).
        lightBufferManager->setRTShadowActive(rtShadowDenoiser && rtShadowDenoiser->isInitialized());

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

        // VK-1430: trace + denoise run at trace dims (half when Half, full otherwise). depth/normal
        // are always full-res; the trace samples them by normalized UV (rt_shadow.glsl line 42), so a
        // half thread grid maps cleanly to [0,1]. In Half mode the upsample (below) reconstructs full
        // res. RR bypass keeps the raw (half) mask flowing to Ray Reconstruction with no upsample.
        auto [tw, th] = rtShadowTraceDims(w, h);

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
            tw, th,
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
                tw, th,
                camData.frameIndex,
                fi);

            // VK-1430: Half mode — reconstruct the full-res mask from the half-res denoised output.
            // The denoiser leaves its output in eShaderReadOnlyOptimal and depth/normal in attachment
            // layout. We re-transition depth/normal to SHADER_READ for the upsample's guide reads,
            // run the upsample (writes full-res output, leaves it in SHADER_READ for set 13), then
            // restore depth/normal to attachment layout so downstream passes see the legacy state.
            if (rtShadowHalfResolution && rtShadowUpsample && rtShadowUpsample->isInitialized())
            {
                // VK-1431: the depth/normal SHADER_READ transition + compute->compute mask barrier +
                // restore dance is shared with the layered path via withUpsampleGuides; only the
                // dispatch (a single 2D denoised view here) differs.
                withUpsampleGuides(cmd, *depthPrepass, [&] {
                    rtShadowUpsample->dispatch(cmd,
                        rtShadowDenoiser->getDenoisedMaskSamplerImageView(),
                        depthPrepass->getDepthImageView(),
                        depthPrepass->getNormalImageView(),
                        tw, th, w, h,
                        rtShadowUpsampleDepthThreshold,
                        rtShadowUpsampleNormalExp,
                        fi);
                });
            }
        }

        if (rtShadowProfiler && rtShadowProfiler->isValid())
        {
            rtShadowProfiler->writeTimestamp(cmd, fi,
                raytracing::RTShadowTimestamp::AfterDenoiser,
                vk::PipelineStageFlagBits::eComputeShader);
        }
    }

    void GPUDrivenRenderer::dispatchRTSpotShadow(vk::CommandBuffer cmd, uint32_t imageIndex)
    {
        if (!isRTSpotShadowReady()) return;

        // Lazy init — create the spot RT pipeline + denoiser and recreate the scene/terrain
        // pipelines with the set-15 spot mask array (preserving the directional set-13 mask).
        if (!rtSpotShadowPipeline)
        {
            auto pipeline = std::make_unique<raytracing::RTLayeredShadowPipeline>(device);
            pipeline->init(depthPrepass->getWidth(), depthPrepass->getHeight(),
                           accelStructManager->getTLASDescriptorLayout());
            if (!pipeline->isInitialized())
                return; // retry next frame
            rtSpotShadowPipeline = std::move(pipeline);

            if (meshShaderPipeline && shadowSystem)
            {
                rtSpotShadowDenoiser = std::make_unique<raytracing::RTLayeredShadowDenoiser>(device);
                rtSpotShadowDenoiser->init(depthPrepass->getWidth(), depthPrepass->getHeight());

                const bool denoised = rtSpotShadowDenoiser && rtSpotShadowDenoiser->isInitialized();
                vk::DescriptorSetLayout spotMaskLayout = denoised
                    ? rtSpotShadowDenoiser->getDenoisedMaskSamplerLayout()
                    : rtSpotShadowPipeline->getShadowMaskSamplerLayout();
                vk::DescriptorSet spotMaskDescSet = denoised
                    ? rtSpotShadowDenoiser->getDenoisedMaskSamplerDescriptorSet()
                    : rtSpotShadowPipeline->getShadowMaskSamplerDescriptorSet();

                vk::DescriptorSetLayout giLayout = (giCascadeManager && giCascadeManager->getProbeStorage())
                    ? giCascadeManager->getProbeStorage()->getSamplingLayout() : nullptr;
                vk::DescriptorSetLayout causticLayout = (water.causticsResources && water.causticsResources->isInitialized())
                    ? water.causticsResources->getDescriptorSetLayout() : nullptr;
                // Preserve the directional RT mask (set 13) if it is already online.
                vk::DescriptorSetLayout rtMaskLayout = getActiveRTShadowMaskLayout();
                vk::DescriptorSet rtMaskDescSet = getActiveRTShadowMaskDescriptorSet();
                // Preserve the point RT mask (set 16) if it is already online.
                vk::DescriptorSetLayout pointMaskLayout = getActiveRTPointShadowMaskLayout();
                vk::DescriptorSet pointMaskDescSet = getActiveRTPointShadowMaskDescriptorSet();

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
                    .worldMaskLayout = currentWorldMaskLayout(),
                    .rtSpotShadowMaskLayout = spotMaskLayout,
                    .rtPointShadowMaskLayout = pointMaskLayout,
                    .selectionCoverageLayout = selectionMaskPipeline ? selectionMaskPipeline->getDescriptorSetLayout() : vk::DescriptorSetLayout{},
                    .colorAttachmentFormats = cachedColorFormats,
                    .depthAttachmentFormat = cachedDepthFormat
                };

                auto restore = [&](MeshShaderPipeline& p)
                {
                    if (rtMaskDescSet) p.updateRTShadowMaskDescriptor(rtMaskDescSet);
                    p.updateRTSpotShadowMaskDescriptor(spotMaskDescSet);
                    if (pointMaskDescSet) p.updateRTPointShadowMaskDescriptor(pointMaskDescSet);
                    if (giLayout && giCascadeManager)
                        p.updateGIProbeDescriptor(giCascadeManager->getProbeStorage()->getSamplingDescSet());
                    if (causticLayout)
                        p.updateCausticDescriptor(water.causticsResources->getDescriptorSet());
                    if (currentWorldMaskLayout() && pluginTextureManager)
                        p.updateWorldMaskDescriptor(pluginTextureManager->getEntityMaskDescriptorSet());
                };

                meshShaderPipeline->recreate(pipelineInfo);
                restore(*meshShaderPipeline);

                if (transparentMeshShaderPipeline)
                {
                    pipelineInfo.transparentMode = true;
                    transparentMeshShaderPipeline->recreate(pipelineInfo);
                    restore(*transparentMeshShaderPipeline);
                    pipelineInfo.transparentMode = false;
                }

                if (wboitMeshShaderPipeline && !cachedWBOITColorFormats.empty())
                {
                    pipelineInfo.colorAttachmentFormats = cachedWBOITColorFormats;
                    pipelineInfo.depthAttachmentFormat = cachedWBOITDepthFormat;
                    pipelineInfo.wboitMode = true;
                    wboitMeshShaderPipeline->recreate(pipelineInfo);
                    restore(*wboitMeshShaderPipeline);
                }

                if (terrain.pipeline)
                {
                    terrain.pipeline->setRTSpotShadowMaskLayout(spotMaskLayout);
                    terrain.pipeline->updateRTSpotShadowMaskDescriptor(spotMaskDescSet);
                    if (pointMaskLayout)
                    {
                        terrain.pipeline->setRTPointShadowMaskLayout(pointMaskLayout);
                        terrain.pipeline->updateRTPointShadowMaskDescriptor(pointMaskDescSet);
                    }
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
                        cachedColorFormats, cachedDepthFormat);
                }
            }
        }

        if (!rtSpotShadowPipeline->isInitialized()) return;

        uint32_t w = depthPrepass->getWidth();
        uint32_t h = depthPrepass->getHeight();
        if (w != 0 && h != 0)
        {
            // VK-1430: trace+denoiser at trace dims (half when Half); layered upsample output at full.
            auto [tw, th] = rtShadowTraceDims(w, h);
            rtSpotShadowPipeline->resize(tw, th);
            if (rtSpotShadowDenoiser && rtSpotShadowDenoiser->isInitialized())
                rtSpotShadowDenoiser->resize(tw, th);
            if (rtShadowHalfResolution && !rtSpotShadowUpsample)
                rtSpotShadowUpsample = std::make_unique<raytracing::RTLayeredShadowUpsamplePipeline>(device);
            if (rtShadowHalfResolution && rtSpotShadowUpsample)
                rtSpotShadowUpsample->resize(w, h);

            // Re-point the set-15 producer (getActive* selects upsample/denoiser/raw per mode).
            vk::DescriptorSet ds = getActiveRTSpotShadowMaskDescriptorSet();
            if (ds)
            {
                if (meshShaderPipeline) meshShaderPipeline->updateRTSpotShadowMaskDescriptor(ds);
                if (transparentMeshShaderPipeline) transparentMeshShaderPipeline->updateRTSpotShadowMaskDescriptor(ds);
                if (wboitMeshShaderPipeline) wboitMeshShaderPipeline->updateRTSpotShadowMaskDescriptor(ds);
                if (terrain.pipeline) terrain.pipeline->updateRTSpotShadowMaskDescriptor(ds);
            }
        }

        if (!accelStructManager || !accelStructManager->isTLASReady()) return;

        const auto& camData = cameraBuffer->getData();
        glm::vec3 camPos = glm::vec3(camData.cameraPosition);

        // Pick the closest/brightest spot lights, assign mask slices; reassigned slices reset history.
        // C5: honor the adaptive spot budget (the profiler shrinks it under sustained RT load).
        // getAppliedSpotBudget() falls back to the base budget when adaptive throttling is off, so
        // this is inert unless the budget evaluator actually engaged this frame.
        uint32_t spotBudget = rtSpotShadowBudget;
        if (rtShadowProfiler)
            spotBudget = std::min<uint32_t>(spotBudget, rtShadowProfiler->getAppliedSpotBudget());
        std::vector<uint32_t> reassigned = lightBufferManager->assignRTSpotSlices(camPos, spotBudget);

        const auto& sliceLights = lightBufferManager->getRTSpotSliceLights();
        const auto& sliceValid = lightBufferManager->getRTSpotSliceValid();

        std::vector<raytracing::RTLayeredDispatchInfo> dispatchList;
        for (uint32_t s = 0; s < raytracing::RTLayeredShadowPipeline::MAX_SLICES; ++s)
        {
            if (!sliceValid[s]) continue;
            int idx = lightBufferManager->getSpotIndexForEntity(sliceLights[s]);
            if (idx < 0) continue;
            const auto& sl = lightBufferManager->getSpotLight(static_cast<uint32_t>(idx));
            raytracing::RTLayeredDispatchInfo info{};
            info.slice = s;
            info.position = sl.position;
            info.range = sl.range;
            info.direction = sl.direction;
            info.cosInnerAngle = sl.cosInnerAngle;
            info.cosOuterAngle = sl.cosOuterAngle;
            dispatchList.push_back(info);
        }

        if (dispatchList.empty())
        {
            lightBufferManager->setRTSpotShadowActive(false);
            return;
        }

        const bool useDenoiser = rtSpotShadowDenoiser && rtSpotShadowDenoiser->isInitialized();
        lightBufferManager->setRTSpotShadowActive(true);

        uint32_t fi = imageIndex % core::MAX_FRAMES_IN_FLIGHT;
        auto [tw, th] = rtShadowTraceDims(w, h);

        // VK-1479 C5: time the spot RT-shadow trace+denoise into the shared profiler pool (slots
        // 6..8) so its cost feeds the adaptive budget. Mirrors the directional path in
        // dispatchRTShadow; the profiler folds these in additively without disturbing the
        // directional readback.
        if (rtShadowProfiler && rtShadowProfiler->isValid())
        {
            rtShadowProfiler->writeTimestamp(cmd, fi,
                raytracing::RTShadowTimestamp::BeforeSpotDispatch,
                vk::PipelineStageFlagBits::eComputeShader);
        }

        rtSpotShadowPipeline->dispatch(cmd,
            depthPrepass->getDepthImageView(), depthPrepass->getDepthImage(),
            depthPrepass->getNormalImageView(), depthPrepass->getNormalImage(),
            accelStructManager->getTLASDescriptorSet(),
            camData.invViewProjection, camPos, camData.farPlane,
            tw, th, dispatchList,
            useDenoiser, // skip final transitions when the denoiser consumes the raw mask
            fi);

        if (rtShadowProfiler && rtShadowProfiler->isValid())
        {
            rtShadowProfiler->writeTimestamp(cmd, fi,
                raytracing::RTShadowTimestamp::AfterSpotDispatch,
                vk::PipelineStageFlagBits::eComputeShader);
        }

        if (useDenoiser)
        {
            std::vector<raytracing::RTLayeredDenoiseInfo> denoiseList;
            for (const auto& d : dispatchList)
            {
                raytracing::RTLayeredDenoiseInfo di{};
                di.slice = d.slice;
                di.rawLayerView = rtSpotShadowPipeline->getShadowMaskLayerView(d.slice);
                di.resetHistory = std::find(reassigned.begin(), reassigned.end(), d.slice) != reassigned.end();
                denoiseList.push_back(di);
            }
            rtSpotShadowDenoiser->dispatch(cmd, denoiseList,
                depthPrepass->getDepthImageView(), depthPrepass->getDepthImage(),
                depthPrepass->getNormalImageView(), depthPrepass->getNormalImage(),
                camData.invViewProjection, camData.viewProjection,
                tw, th, camData.frameIndex, fi);

            // VK-1430: Half mode — reconstruct full-res per-slice masks from the half-res denoised
            // slices (see dispatchRTShadow for the depth/normal layout dance).
            if (rtShadowHalfResolution && rtSpotShadowUpsample && rtSpotShadowUpsample->isInitialized())
                upsampleLayeredRTShadow(cmd, *rtSpotShadowUpsample, *rtSpotShadowDenoiser,
                                        dispatchList, tw, th, w, h, fi);
        }

        // VK-1479 C5: close the spot trace+denoise span (slot 8). Written unconditionally — like the
        // directional AfterDenoiser — so the span is valid whether or not the denoiser ran.
        if (rtShadowProfiler && rtShadowProfiler->isValid())
        {
            rtShadowProfiler->writeTimestamp(cmd, fi,
                raytracing::RTShadowTimestamp::AfterSpotDenoiser,
                vk::PipelineStageFlagBits::eComputeShader);
        }
    }

    void GPUDrivenRenderer::dispatchRTPointShadow(vk::CommandBuffer cmd, uint32_t imageIndex)
    {
        if (!isRTPointShadowReady()) return;

        // Lazy init — create the point RT pipeline + denoiser and recreate the scene/terrain
        // pipelines with the set-16 point mask array (preserving directional set 13 + spot set 15).
        if (!rtPointShadowPipeline)
        {
            auto pipeline = std::make_unique<raytracing::RTLayeredShadowPipeline>(device);
            pipeline->init(depthPrepass->getWidth(), depthPrepass->getHeight(),
                           accelStructManager->getTLASDescriptorLayout());
            if (!pipeline->isInitialized())
                return; // retry next frame
            rtPointShadowPipeline = std::move(pipeline);

            if (meshShaderPipeline && shadowSystem)
            {
                rtPointShadowDenoiser = std::make_unique<raytracing::RTLayeredShadowDenoiser>(device);
                rtPointShadowDenoiser->init(depthPrepass->getWidth(), depthPrepass->getHeight());

                const bool denoised = rtPointShadowDenoiser && rtPointShadowDenoiser->isInitialized();
                vk::DescriptorSetLayout pointMaskLayout = denoised
                    ? rtPointShadowDenoiser->getDenoisedMaskSamplerLayout()
                    : rtPointShadowPipeline->getShadowMaskSamplerLayout();
                vk::DescriptorSet pointMaskDescSet = denoised
                    ? rtPointShadowDenoiser->getDenoisedMaskSamplerDescriptorSet()
                    : rtPointShadowPipeline->getShadowMaskSamplerDescriptorSet();

                vk::DescriptorSetLayout giLayout = (giCascadeManager && giCascadeManager->getProbeStorage())
                    ? giCascadeManager->getProbeStorage()->getSamplingLayout() : nullptr;
                vk::DescriptorSetLayout causticLayout = (water.causticsResources && water.causticsResources->isInitialized())
                    ? water.causticsResources->getDescriptorSetLayout() : nullptr;
                // Preserve the directional RT mask (set 13) and spot RT mask (set 15) if already online.
                vk::DescriptorSetLayout rtMaskLayout = getActiveRTShadowMaskLayout();
                vk::DescriptorSet rtMaskDescSet = getActiveRTShadowMaskDescriptorSet();
                vk::DescriptorSetLayout spotMaskLayout = getActiveRTSpotShadowMaskLayout();
                vk::DescriptorSet spotMaskDescSet = getActiveRTSpotShadowMaskDescriptorSet();

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
                    .worldMaskLayout = currentWorldMaskLayout(),
                    .rtSpotShadowMaskLayout = spotMaskLayout,
                    .rtPointShadowMaskLayout = pointMaskLayout,
                    .selectionCoverageLayout = selectionMaskPipeline ? selectionMaskPipeline->getDescriptorSetLayout() : vk::DescriptorSetLayout{},
                    .colorAttachmentFormats = cachedColorFormats,
                    .depthAttachmentFormat = cachedDepthFormat
                };

                auto restore = [&](MeshShaderPipeline& p)
                {
                    if (rtMaskDescSet) p.updateRTShadowMaskDescriptor(rtMaskDescSet);
                    if (spotMaskDescSet) p.updateRTSpotShadowMaskDescriptor(spotMaskDescSet);
                    p.updateRTPointShadowMaskDescriptor(pointMaskDescSet);
                    if (giLayout && giCascadeManager)
                        p.updateGIProbeDescriptor(giCascadeManager->getProbeStorage()->getSamplingDescSet());
                    if (causticLayout)
                        p.updateCausticDescriptor(water.causticsResources->getDescriptorSet());
                    if (currentWorldMaskLayout() && pluginTextureManager)
                        p.updateWorldMaskDescriptor(pluginTextureManager->getEntityMaskDescriptorSet());
                };

                meshShaderPipeline->recreate(pipelineInfo);
                restore(*meshShaderPipeline);

                if (transparentMeshShaderPipeline)
                {
                    pipelineInfo.transparentMode = true;
                    transparentMeshShaderPipeline->recreate(pipelineInfo);
                    restore(*transparentMeshShaderPipeline);
                    pipelineInfo.transparentMode = false;
                }

                if (wboitMeshShaderPipeline && !cachedWBOITColorFormats.empty())
                {
                    pipelineInfo.colorAttachmentFormats = cachedWBOITColorFormats;
                    pipelineInfo.depthAttachmentFormat = cachedWBOITDepthFormat;
                    pipelineInfo.wboitMode = true;
                    wboitMeshShaderPipeline->recreate(pipelineInfo);
                    restore(*wboitMeshShaderPipeline);
                }

                if (terrain.pipeline)
                {
                    terrain.pipeline->setRTPointShadowMaskLayout(pointMaskLayout);
                    terrain.pipeline->updateRTPointShadowMaskDescriptor(pointMaskDescSet);
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
                        cachedColorFormats, cachedDepthFormat);
                }
            }
        }

        if (!rtPointShadowPipeline->isInitialized()) return;

        uint32_t w = depthPrepass->getWidth();
        uint32_t h = depthPrepass->getHeight();
        if (w != 0 && h != 0)
        {
            // VK-1430: trace+denoiser at trace dims (half when Half); layered upsample output at full.
            auto [tw, th] = rtShadowTraceDims(w, h);
            rtPointShadowPipeline->resize(tw, th);
            if (rtPointShadowDenoiser && rtPointShadowDenoiser->isInitialized())
                rtPointShadowDenoiser->resize(tw, th);
            if (rtShadowHalfResolution && !rtPointShadowUpsample)
                rtPointShadowUpsample = std::make_unique<raytracing::RTLayeredShadowUpsamplePipeline>(device);
            if (rtShadowHalfResolution && rtPointShadowUpsample)
                rtPointShadowUpsample->resize(w, h);

            // Re-point the set-16 producer (getActive* selects upsample/denoiser/raw per mode).
            vk::DescriptorSet ds = getActiveRTPointShadowMaskDescriptorSet();
            if (ds)
            {
                if (meshShaderPipeline) meshShaderPipeline->updateRTPointShadowMaskDescriptor(ds);
                if (transparentMeshShaderPipeline) transparentMeshShaderPipeline->updateRTPointShadowMaskDescriptor(ds);
                if (wboitMeshShaderPipeline) wboitMeshShaderPipeline->updateRTPointShadowMaskDescriptor(ds);
                if (terrain.pipeline) terrain.pipeline->updateRTPointShadowMaskDescriptor(ds);
            }
        }

        if (!accelStructManager || !accelStructManager->isTLASReady()) return;

        const auto& camData = cameraBuffer->getData();
        glm::vec3 camPos = glm::vec3(camData.cameraPosition);

        // Pick the closest/brightest point lights, assign mask slices; reassigned slices reset history.
        // C5: honor the adaptive point budget (inert unless the profiler shrank it this frame).
        uint32_t pointBudget = rtPointShadowBudget;
        if (rtShadowProfiler)
            pointBudget = std::min<uint32_t>(pointBudget, rtShadowProfiler->getAppliedPointBudget());
        std::vector<uint32_t> reassigned = lightBufferManager->assignRTPointSlices(camPos, pointBudget);

        const auto& sliceLights = lightBufferManager->getRTPointSliceLights();
        const auto& sliceValid = lightBufferManager->getRTPointSliceValid();

        std::vector<raytracing::RTLayeredDispatchInfo> dispatchList;
        for (uint32_t s = 0; s < raytracing::RTLayeredShadowPipeline::MAX_SLICES; ++s)
        {
            if (!sliceValid[s]) continue;
            int idx = lightBufferManager->getPointIndexForEntity(sliceLights[s]);
            if (idx < 0) continue;
            const auto& pl = lightBufferManager->getPointLight(static_cast<uint32_t>(idx));
            raytracing::RTLayeredDispatchInfo info{};
            info.slice = s;
            info.position = pl.position;
            info.range = pl.radius;
            // Point light = spot light with the cone disabled (cosOuterAngle = -2.0 sentinel).
            info.direction = glm::vec3(0.0f, 0.0f, 1.0f); // ignored when the cone is disabled
            info.cosInnerAngle = -2.0f;
            info.cosOuterAngle = -2.0f;
            dispatchList.push_back(info);
        }

        if (dispatchList.empty())
        {
            lightBufferManager->setRTPointShadowActive(false);
            return;
        }

        const bool useDenoiser = rtPointShadowDenoiser && rtPointShadowDenoiser->isInitialized();
        lightBufferManager->setRTPointShadowActive(true);

        uint32_t fi = imageIndex % core::MAX_FRAMES_IN_FLIGHT;
        auto [tw, th] = rtShadowTraceDims(w, h);

        // VK-1479 C5: time the point RT-shadow trace+denoise into the shared profiler pool (slots
        // 9..11). Because the shared query pool only supports prefix reads, the profiler folds
        // point cost into the adaptive budget only when spot RT also ran that frame; otherwise the
        // point cost is conservatively omitted (never over-counted). See RTShadowProfiler.
        if (rtShadowProfiler && rtShadowProfiler->isValid())
        {
            rtShadowProfiler->writeTimestamp(cmd, fi,
                raytracing::RTShadowTimestamp::BeforePointDispatch,
                vk::PipelineStageFlagBits::eComputeShader);
        }

        rtPointShadowPipeline->dispatch(cmd,
            depthPrepass->getDepthImageView(), depthPrepass->getDepthImage(),
            depthPrepass->getNormalImageView(), depthPrepass->getNormalImage(),
            accelStructManager->getTLASDescriptorSet(),
            camData.invViewProjection, camPos, camData.farPlane,
            tw, th, dispatchList,
            useDenoiser, // skip final transitions when the denoiser consumes the raw mask
            fi);

        if (rtShadowProfiler && rtShadowProfiler->isValid())
        {
            rtShadowProfiler->writeTimestamp(cmd, fi,
                raytracing::RTShadowTimestamp::AfterPointDispatch,
                vk::PipelineStageFlagBits::eComputeShader);
        }

        if (useDenoiser)
        {
            std::vector<raytracing::RTLayeredDenoiseInfo> denoiseList;
            for (const auto& d : dispatchList)
            {
                raytracing::RTLayeredDenoiseInfo di{};
                di.slice = d.slice;
                di.rawLayerView = rtPointShadowPipeline->getShadowMaskLayerView(d.slice);
                di.resetHistory = std::find(reassigned.begin(), reassigned.end(), d.slice) != reassigned.end();
                denoiseList.push_back(di);
            }
            rtPointShadowDenoiser->dispatch(cmd, denoiseList,
                depthPrepass->getDepthImageView(), depthPrepass->getDepthImage(),
                depthPrepass->getNormalImageView(), depthPrepass->getNormalImage(),
                camData.invViewProjection, camData.viewProjection,
                tw, th, camData.frameIndex, fi);

            // VK-1430: Half mode — reconstruct full-res per-slice masks from the half-res denoised slices.
            if (rtShadowHalfResolution && rtPointShadowUpsample && rtPointShadowUpsample->isInitialized())
                upsampleLayeredRTShadow(cmd, *rtPointShadowUpsample, *rtPointShadowDenoiser,
                                        dispatchList, tw, th, w, h, fi);
        }

        // VK-1479 C5: close the point trace+denoise span (slot 11), written unconditionally.
        if (rtShadowProfiler && rtShadowProfiler->isValid())
        {
            rtShadowProfiler->writeTimestamp(cmd, fi,
                raytracing::RTShadowTimestamp::AfterPointDenoiser,
                vk::PipelineStageFlagBits::eComputeShader);
        }
    }

    void GPUDrivenRenderer::applyRTShadowSettings(const types::RTShadowSettings& settings)
    {
        rtShadowEnabled = settings.enabled;
        // When RT is disabled, clear the runtime flag so the fragment shaders fall back to
        // the directional VSM clipmap instead of sampling a stale RT mask. (dispatchRTShadow
        // only ever sets it true, so nothing else would clear it on toggle-off.)
        if (!rtShadowEnabled && lightBufferManager)
            lightBufferManager->setRTShadowActive(false);
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

        // VK-1175: optional RT override for spot lights. Shares the directional ray/denoiser
        // tunables; gated by its own enable flag and budget, OFF by default.
        rtSpotShadowEnabled = settings.spotEnabled;
        rtSpotShadowBudget = settings.spotBudget;
        // Clear the runtime flag on toggle-off so the fragment shaders resume VSM with no stale mask.
        if (!rtSpotShadowEnabled && lightBufferManager)
            lightBufferManager->setRTSpotShadowActive(false);
        if (rtSpotShadowPipeline)
        {
            rtSpotShadowPipeline->setMaxRayDistance(settings.maxRayDistance);
            rtSpotShadowPipeline->setNormalBias(settings.normalBias);
            rtSpotShadowPipeline->setRayTMin(settings.rayTMin);
        }
        if (rtSpotShadowDenoiser)
        {
            rtSpotShadowDenoiser->setTemporalBlend(settings.temporalBlend);
            rtSpotShadowDenoiser->setDepthThreshold(settings.depthThreshold);
            rtSpotShadowDenoiser->setNormalThreshold(settings.normalThreshold);
            rtSpotShadowDenoiser->setSpatialPhiDepth(settings.spatialPhiDepth);
            rtSpotShadowDenoiser->setSpatialPhiNormal(settings.spatialPhiNormal);
            rtSpotShadowDenoiser->setSpatialPasses(settings.spatialPasses);
        }

        // VK-1176: optional RT override for point lights. Shares the directional ray/denoiser
        // tunables; gated by its own enable flag and budget, OFF by default.
        rtPointShadowEnabled = settings.pointEnabled;
        rtPointShadowBudget = settings.pointBudget;
        // Clear the runtime flag on toggle-off so the fragment shaders resume VSM with no stale mask.
        if (!rtPointShadowEnabled && lightBufferManager)
            lightBufferManager->setRTPointShadowActive(false);
        if (rtPointShadowPipeline)
        {
            rtPointShadowPipeline->setMaxRayDistance(settings.maxRayDistance);
            rtPointShadowPipeline->setNormalBias(settings.normalBias);
            rtPointShadowPipeline->setRayTMin(settings.rayTMin);
        }
        if (rtPointShadowDenoiser)
        {
            rtPointShadowDenoiser->setTemporalBlend(settings.temporalBlend);
            rtPointShadowDenoiser->setDepthThreshold(settings.depthThreshold);
            rtPointShadowDenoiser->setNormalThreshold(settings.normalThreshold);
            rtPointShadowDenoiser->setSpatialPhiDepth(settings.spatialPhiDepth);
            rtPointShadowDenoiser->setSpatialPhiNormal(settings.spatialPhiNormal);
            rtPointShadowDenoiser->setSpatialPasses(settings.spatialPasses);
        }

        // VK-1430: shared resolution scale for directional + spot + point RT shadows. Upsample
        // edge-stopping reuses the denoiser depth threshold + spatial normal exponent.
        rtShadowUpsampleDepthThreshold = settings.depthThreshold;
        rtShadowUpsampleNormalExp = settings.spatialPhiNormal;

        const bool wantHalf = settings.shadowResolutionScale == types::ShadowResolutionScale::Half;
        if (wantHalf != rtShadowHalfResolution)
        {
            // Flip the shared flag so the per-frame resize blocks size trace+denoiser to the new dims
            // (their resize() early-outs on unchanged dims, so the flip drives reallocation), lazily
            // create the upsample pipelines (Half), and select the right producer via
            // getActive*MaskDescriptorSet(). The set-13/15/16 layouts are identical between denoiser
            // and upsample, so NO pipeline recreate is needed — the resize blocks re-copy the producer
            // into the VK-1398 ring via updateRT*MaskDescriptor.
            rtShadowHalfResolution = wantHalf;

            // Back to Full: drop the upsample objects so Full mode holds zero extra GPU resources
            // (reset() idles the device). Half-mode creation happens lazily in the resize blocks.
            if (!wantHalf)
            {
                rtShadowUpsample.reset();
                rtSpotShadowUpsample.reset();
                rtPointShadowUpsample.reset();
            }
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
