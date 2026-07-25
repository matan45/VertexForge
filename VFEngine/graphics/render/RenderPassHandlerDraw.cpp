#include "RenderPassHandler.hpp"
#include "graph/RenderGraph.hpp"
#include "print/Log.hpp"
#include "decal/DecalPipeline.hpp"
#include "../core/SwapChain.hpp"
#include "../core/Device.hpp"
#include "../core/ImageUtilities.hpp"
#include "../core/ThreadCommandPoolManager.hpp"
#include "ClearColor.hpp"
#include "IBL.hpp"
#include "DebugRenderer.hpp"
#include "mesh/StaticMeshPipeline.hpp"
#include "mesh/MeshTypes.hpp"
#include "billboard/BillboardPipeline.hpp"
#include "text/TextPipeline.hpp"
#include "ui/UIRenderPipeline.hpp"
#include "ui/UITextPipeline.hpp"
#include "occlusion/CameraOcclusionManager.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "postprocess/PostProcessPipeline.hpp"
#include "volumetric/VolumetricFogComposite.hpp"
#include "gi/SSGIPipeline.hpp"
#include "atmosphere/AtmospherePipeline.hpp"
#include "cloud/CloudPipeline.hpp"
#include "transparency/WBOITPipeline.hpp"
#include "custom/CustomPipelineManager.hpp"
#include "custom/PluginTextureManager.hpp"
#include "lighting/GPULightBufferManager.hpp"
#include "lighting/ClusterGridManager.hpp"
#include "lighting/LightCullingPipeline.hpp"
#include "shadow/ShadowSystem.hpp"
#include "upscaling/UpscaleManager.hpp"
#include "../../services/providers/vfx/IVFXRuntimeProvider.hpp"
#include "../../services/providers/terrain/ITerrainRenderProvider.hpp"
#include "../../services/providers/terrain/IOceanRenderProvider.hpp"
#include "../../services/providers/vegetation/IGrassRenderProvider.hpp"
#include "vfx/distortion/DistortionResources.hpp"
#include "vfx/distortion/VFXDistortionComposite.hpp"
#include "../core/DynamicRenderingHelpers.hpp"
#include "threading/JobSystem.hpp"
#include "stats/FrameDrawStats.hpp"
#include "stats/GpuPassStats.hpp"
#include "graph/RenderGraphProfiler.hpp"
#include <chrono>

namespace render
{
    void RenderPassHandler::draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        // VK-1529 tier 1: open the whole-frame GPU span before this frame records
        // anything, and publish the span this slot already holds. Always on — the
        // status bar needs a GPU number with the profiler window closed.
        beginFrameTiming(commandBuffer, imageIndex);

        // Plugin texture CPU->GPU uploads — recorded before the frame graph so the
        // copies land outside any render pass and complete before the scene samples them.
        // Publish the previous frame's draw-call total and reset the accumulator for
        // this frame, before any pass records draws (VK-1368).
        FrameDrawStats::beginFrame();

        if (pluginTextureManager)
        {
            pluginTextureManager->flushUploads(commandBuffer);

            // VK-1488: repoint every UI-exposed plugin texture into the UI/billboard bindless
            // table for the swapchain image being recorded. Same safety window as the RTT
            // repoint — RenderManager already waited imagesInFlight[imageIndex], so this slot
            // is idle (UpdateAfterBind). A plugin texture's view/sampler are stable for its
            // lifetime, so once a slot matches this is a cheap no-op; first-seen slots fill
            // lazily and a swapchain resize re-populates automatically next frame.
            pluginTextureManager->forEachUITextureBinding(
                [&](const std::string& key, vk::ImageView view, vk::Sampler sampler)
                {
                    registerExternalTexture(key, imageIndex, view, sampler);
                });
        }

        // Lit plugin custom pipelines: pick up the RT shadow mask layout once the
        // RT shadow pipeline comes online — rebuilds them with RT_SHADOW_ENABLED
        // + set 13. Cheap no-op while the layout is unchanged.
        syncCustomPipelineRTShadow();

        // GPU pass profiling: readback last frame's timestamps before this
        // frame's graph records new ones into the same per-frame pool slot
        syncGraphProfiler(imageIndex);

        frameGraph->reset();
        importFrameResources(imageIndex);
        buildFrameGraph(commandBuffer, imageIndex);
        frameGraph->compile();

        // VK-1532: flag the scene mesh cache as "recording" so any material pipeline created
        // synchronously during graph execution (a warm-up miss) is logged/asserted. execute()
        // records the SceneMeshes pass inline and joins parallel-recording jobs before it
        // returns, so this single flag covers the render thread and those worker threads.
        if (meshPipeline) meshPipeline->setFrameRecording(true);
        frameGraph->execute(commandBuffer, imageIndex);
        if (meshPipeline) meshPipeline->setFrameRecording(false);

        // Plugin custom draws are enqueued per frame — drop them whether or not
        // the scene pass consumed them (e.g. GPU-driven renderer disabled).
        if (customPipelineManager) customPipelineManager->endFrame();

        // VK-1529 tier 1: close the span once every command this frame records is in.
        endFrameTiming(commandBuffer, imageIndex);
    }

    void RenderPassHandler::beginPipelineWarmup(std::vector<std::string> extraPaths)
    {
        if (meshPipeline)
        {
            meshPipeline->beginPipelineWarmup(std::move(extraPaths));
        }
    }

    services::PipelineWarmupStats RenderPassHandler::getPipelineWarmupStats() const
    {
        return meshPipeline ? meshPipeline->getPipelineWarmupStats() : services::PipelineWarmupStats{};
    }

    void RenderPassHandler::beginFrameTiming(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        frameTimeActiveThisFrame = false;
        if (frameTimeUnsupported) return;

        auto& sink = GpuPassStats::instance();

        if (!frameTimePoolInitialized)
        {
            // Two queries per slot: the frame's opening and closing boundary.
            if (!frameTimePool.init(device, 2))
            {
                frameTimeUnsupported = true;
                sink.markUnsupported();
                return;
            }
            frameTimePoolInitialized = true;
        }

        if (!frameTimePool.isValid()) return;

        const uint32_t fi = imageIndex % core::MAX_FRAMES_IN_FLIGHT;

        // Publish this slot's previous occupant BEFORE resetting it. The pool is
        // keyed by imageIndex % MAX_FRAMES_IN_FLIGHT — the same modulus
        // OffScreenViewPort's inFlightFences use, and it waits on that fence before
        // recording — so the frame that last used this slot is complete and its two
        // queries are readable. Reading the slot we are about to reset, rather than
        // inferring a "previous" slot from imageIndex, is what makes every write get
        // read exactly once (see RenderGraphProfiler::readbackAndUpdate).
        //
        // Consequence worth knowing: a slot is revisited on its own cadence, so with
        // an odd image count two adjacent spans can be published out of order (frame
        // N+1's slot may come round after frame N+2's). Displacement is bounded by the
        // image count — 1-2 samples out of the 120-entry ring — which is invisible on
        // the plot and preferable to the alternative of dropping the later sample and
        // losing a hitch with it.
        std::vector<uint64_t> ts;
        if (frameTimeSlotWritten[fi] &&
            frameTimePool.readResults(device.getLogicalDevice(), imageIndex, ts, 2) &&
            ts.size() >= 2)
        {
            const float ms = frameTimePool.toMilliseconds(ts[0], ts[1]);
            emaFrameGpuMs = frameGpuEmaSeeded
                ? (timing::EMA_ALPHA * ms + (1.0f - timing::EMA_ALPHA) * emaFrameGpuMs)
                : ms;
            frameGpuEmaSeeded = true;
            sink.publishFrameTime(ms, emaFrameGpuMs);
        }

        // Reset must precede any write and must be outside a render pass — draw()
        // brackets the whole command buffer, so we are.
        frameTimePool.resetFrame(commandBuffer, imageIndex);
        // Top-of-pipe is the right "frame start" here even though it is the wrong
        // per-pass start: nothing precedes it in this command buffer.
        frameTimePool.writeTimestamp(commandBuffer, imageIndex, 0,
                                     vk::PipelineStageFlagBits::eTopOfPipe);
        frameTimeSlotWritten[fi] = true;
        frameTimeActiveThisFrame = true;
    }

    void RenderPassHandler::endFrameTiming(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        if (!frameTimeActiveThisFrame) return;

        // Bottom-of-pipe: latches once everything recorded above has drained.
        frameTimePool.writeTimestamp(commandBuffer, imageIndex, 1,
                                     vk::PipelineStageFlagBits::eBottomOfPipe);
        frameTimeActiveThisFrame = false;
    }

    custom::CustomLightingSets RenderPassHandler::buildCustomLightingSets(vk::DescriptorSet iblDescriptorSet) const
    {
        // Same per-frame sets the terrain pipeline consumes (see
        // GPUDrivenRenderer::renderTerrainDraw); lit custom pipelines are
        // skipped while any set is missing.
        custom::CustomLightingSets sets{};
        if (!gpuDrivenRendererInitialized || !gpuDrivenRenderer) return sets;

        sets.ibl = iblDescriptorSet;
        if (auto* lbm = gpuDrivenRenderer->getLightBufferManager()) sets.lights = lbm->getDescriptorSet();
        if (auto* cgm = gpuDrivenRenderer->getClusterGridManager()) sets.clusterParams = cgm->getDescriptorSet();
        if (auto* lcp = gpuDrivenRenderer->getLightCullingPipeline()) sets.clusterIndices = lcp->getDescriptorSet();

        auto* shadowSystem = gpuDrivenRenderer->getShadowSystem();
        if (shadowSystem && shadowSystem->isInitialized())
        {
            sets.shadowData = shadowSystem->getShadowDataDescSet();
            sets.shadowTextures = shadowSystem->getShadowTextureDescSet();
        }

        // Set 13 — fetched per frame so raw <-> denoised switches and resizes
        // are picked up automatically; null while RT shadows are offline.
        sets.rtShadowMask = gpuDrivenRenderer->getActiveRTShadowMaskDescriptorSet();
        return sets;
    }

    void RenderPassHandler::syncCustomPipelineRTShadow()
    {
        if (!customPipelineManager || !gpuDrivenRendererInitialized || !gpuDrivenRenderer) return;
        customPipelineManager->setRTShadowMaskLayout(gpuDrivenRenderer->getActiveRTShadowMaskLayout());
    }

    void RenderPassHandler::syncGraphProfiler(uint32_t imageIndex)
    {
        if (!graphProfiler || graphProfilerUnsupported) return;

        auto& sink = GpuPassStats::instance();
        bool wanted = sink.isEnabledRequested();

        if (wanted && !graphProfilerInitialized)
        {
            // 2 timestamp queries per pass; 64 is far above the current graph size
            constexpr uint32_t kMaxProfiledPasses = 64;
            if (graphProfiler->init(device, kMaxProfiledPasses))
            {
                frameGraph->setProfiler(graphProfiler.get());
                graphProfilerInitialized = true;
            }
            else
            {
                graphProfilerUnsupported = true;
                sink.markUnsupported();
                return;
            }
        }

        if (!graphProfilerInitialized) return;

        if (graphProfiler->isEnabled() != wanted)
        {
            graphProfiler->setEnabled(wanted);
            if (!wanted)
            {
                sink.clear();
                vtEma.clear();
                droppedGpuSamples = 0;
                gpuSampleSeen = false;
                // Nothing resets the VT pool while disabled, so its queries stay
                // AVAILABLE. Drop the slot bookkeeping too, or a re-enable in a frame
                // where the GPU-driven mesh path does not run (leaving beginVTTimestamps
                // uncalled, so the slot is never re-armed) would read pre-disable
                // timestamps and publish them as current rows.
                for (auto& count : vtSlotQueryCount) count = 0;
                for (auto& names : vtSlotScopeNames) names.clear();
            }
        }

        if (!wanted) return;

        // VK-1480: bring up the aux VT timestamp pool alongside the graph profiler.
        // Reaching here means timestamps are supported (graphProfiler init succeeded), so
        // mark initialized unconditionally — a failed init just leaves the pool invalid and
        // every VT scope/readback below no-ops. Avoids re-init attempts across frames.
        if (!vtTimestampPoolInitialized)
        {
            vtTimestampPool.init(device, kVTTimestampScopes * 2);
            vtTimestampPoolInitialized = true;
        }

        if (!graphProfiler->readbackAndUpdate(device.getLogicalDevice(), imageIndex))
        {
            // Readback is non-blocking, so a frame whose results are not ready
            // contributes no sample. Republishing the last numbers would make a
            // dropped sample indistinguishable from a real one in the history
            // plot — count it and publish nothing instead.
            if (gpuSampleSeen) ++droppedGpuSamples;
            return;
        }
        gpuSampleSeen = true;

        const auto& stats = graphProfiler->getStats();
        GpuFrameStats out;
        out.valid = stats.passCount > 0;
        out.totalMs = stats.totalMs;
        out.emaTotalMs = stats.emaTotalMs;
        out.barrierCount = stats.barrierCount;
        out.barrierFlushCount = stats.barrierFlushCount;
        out.droppedSamples = droppedGpuSamples;
        out.passTimings.reserve(stats.passTimings.size());
        for (const auto& pass : stats.passTimings)
        {
            out.passTimings.push_back({pass.name, pass.ms, pass.emaMs, false});
        }

        // VK-1480: append the aux VT scope timings for this slot's completed frame so
        // they appear as rows in the same profiler window. Read the slot we are about
        // to reset, for the same fence reason as the graph profiler's readback — the
        // VT bookkeeping is already slot-keyed, and beginVTTimestamps only clears it
        // later, inside the graph's execute. readResults is non-blocking, so a slot
        // whose frame is still in flight simply contributes no rows this frame.
        if (vtTimestampPoolInitialized && vtTimestampPool.isValid())
        {
            const uint32_t fi = imageIndex % core::MAX_FRAMES_IN_FLIGHT;
            uint32_t writtenQueries = vtSlotQueryCount[fi];
            const auto& names = vtSlotScopeNames[fi];
            if (writtenQueries > 0 && !names.empty())
            {
                std::vector<uint64_t> ts;
                if (vtTimestampPool.readResults(device.getLogicalDevice(), imageIndex, ts, writtenQueries))
                {
                    ++vtEmaFrame;
                    for (size_t i = 0; i < names.size() && (i * 2 + 1) < ts.size(); ++i)
                    {
                        float ms = vtTimestampPool.toMilliseconds(ts[i * 2], ts[i * 2 + 1]);
                        // isAux: every VT scope is recorded INSIDE the SceneMeshes
                        // pass, so its cost is already in totalMs. Flagged so the UI
                        // keeps it out of the share-of-frame denominator instead of
                        // double-counting it as a sibling pass.
                        out.passTimings.push_back(
                            {names[i], ms, vtEma.update(names[i], ms, vtEmaFrame), true});
                    }
                    vtEma.prune(vtEmaFrame, kVtEmaMaxAgeFrames);
                }
            }
        }

        sink.publish(std::move(out));
    }

    void RenderPassHandler::beginVTTimestamps(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        vtTimestampsActiveThisFrame = false;
        vtQueryCursor = 0;
        if (!vtTimestampPoolInitialized || !vtTimestampPool.isValid()) return;
        if (!GpuPassStats::instance().isEnabledRequested()) return;

        // Reset this slot's queries before any writeTimestamp. Must be outside a render
        // pass — the VT commands run between graph passes, so we are here.
        vtTimestampPool.resetFrame(commandBuffer, imageIndex);
        uint32_t fi = imageIndex % core::MAX_FRAMES_IN_FLIGHT;
        vtSlotScopeNames[fi].clear();
        vtSlotQueryCount[fi] = 0;
        vtTimestampsActiveThisFrame = true;
    }

    uint32_t RenderPassHandler::vtScopeBegin(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                             const char* name) const
    {
        uint32_t startIndex = vtScopeAlloc(imageIndex, name);
        vtScopeBeginAt(commandBuffer, imageIndex, startIndex);
        return startIndex;
    }

    uint32_t RenderPassHandler::vtScopeAlloc(uint32_t imageIndex, const char* name) const
    {
        if (!vtTimestampsActiveThisFrame) return UINT32_MAX;
        if (vtQueryCursor + 2 > kVTTimestampScopes * 2) return UINT32_MAX; // pool full — drop extra scope
        uint32_t startIndex = vtQueryCursor;
        vtSlotScopeNames[imageIndex % core::MAX_FRAMES_IN_FLIGHT].push_back(name);
        vtQueryCursor += 2;
        return startIndex;
    }

    void RenderPassHandler::vtScopeBeginAt(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                           uint32_t startQueryIndex) const
    {
        if (startQueryIndex == UINT32_MAX) return;
        vtTimestampPool.writeTimestamp(commandBuffer, imageIndex, startQueryIndex,
                                       vk::PipelineStageFlagBits::eTopOfPipe);
    }

    void RenderPassHandler::vtScopeEnd(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                       uint32_t startQueryIndex) const
    {
        if (startQueryIndex == UINT32_MAX) return;
        vtTimestampPool.writeTimestamp(commandBuffer, imageIndex, startQueryIndex + 1,
                                       vk::PipelineStageFlagBits::eBottomOfPipe);
    }

    void RenderPassHandler::endVTTimestamps(uint32_t imageIndex) const
    {
        if (!vtTimestampsActiveThisFrame) return;
        vtSlotQueryCount[imageIndex % core::MAX_FRAMES_IN_FLIGHT] = vtQueryCursor;
        vtTimestampsActiveThisFrame = false;
    }

    void RenderPassHandler::executeDistortionPass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        if (!vfxRuntimeProvider || !vfxRuntimeProvider->isInitialized() || !vfxRuntimeProvider->hasDistortionEmitters())
            return;

        // Lazy init: create distortion resources on first use
        if (!distortionInitialized)
            initDistortionPass();

        if (!distortionInitialized || !distortionResources || !distortionResources->isInitialized())
            return;

        auto extent = distortionResources->getExtent();

        // 1. Copy scene color before distortion
        distortionResources->copySceneColor(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            extent.width, extent.height);

        // 2. Distortion vector pass (clear + render distortion emitters into R16G16 buffer)
        // Transition distortion image: ShaderReadOnly -> ColorAttachmentOptimal
        {
            vk::ImageMemoryBarrier toColorAttach{};
            toColorAttach.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            toColorAttach.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
            toColorAttach.image = distortionResources->getDistortionImage();
            toColorAttach.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
            toColorAttach.srcAccessMask = vk::AccessFlagBits::eShaderRead;
            toColorAttach.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                                          vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                          {}, {}, {}, toColorAttach);
        }

        // Transition scene depth: AttachmentOptimal -> ReadOnlyOptimal for depth sampling
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

        auto colorAttach = core::colorClear(distortionResources->getDistortionView(),
            vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}});
        auto depthAttach = core::depthReadOnly(distortionResources->getSceneDepthView());

        core::DynamicRenderingInfo dynInfo{};
        dynInfo.extent = extent;
        dynInfo.colorAttachments = {colorAttach};
        dynInfo.depthAttachment = depthAttach;

        core::beginDynamicRendering(commandBuffer, dynInfo);

        vk::Viewport viewport{0.0f, 0.0f,
            static_cast<float>(extent.width), static_cast<float>(extent.height),
            0.0f, 1.0f};
        commandBuffer.setViewport(0, viewport);
        vk::Rect2D scissor{{0, 0}, extent};
        commandBuffer.setScissor(0, scissor);

        vfxRuntimeProvider->recordDistortionDrawCommands(commandBuffer);

        core::endDynamicRendering(commandBuffer);

        // Restore scene depth: ReadOnlyOptimal -> AttachmentOptimal
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

        // Transition distortion image back: ColorAttachmentOptimal -> ShaderReadOnlyOptimal
        {
            vk::ImageMemoryBarrier toShaderRead{};
            toShaderRead.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
            toShaderRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            toShaderRead.image = distortionResources->getDistortionImage();
            toShaderRead.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
            toShaderRead.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            toShaderRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                          vk::PipelineStageFlagBits::eFragmentShader,
                                          {}, {}, {}, toShaderRead);
        }

        // 3. Composite pass: apply distortion to scene color
        distortionComposite->record(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImageView,
            extent,
            distortionResources->getCompositeDescriptorSet());
    }

    void RenderPassHandler::capturePreTransparencyColor(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        auto* upscaleManager = device.getUpscaleManager();
        if (!upscaleManager || !upscaleManager->isActive()
            || !offscreenResources.upscaleResourcesCreated
            || !offscreenResources.preTransparencyColor.image)
            return;

        auto renderRes = upscaleManager->getResolutionManager().getRenderResolution();
        vk::Image srcColorImage = offscreenResources.colorImages[imageIndex].colorImage;
        vk::Image dstImage = offscreenResources.preTransparencyColor.image;

        // Destination is fully overwritten - discard previous contents
        vk::ImageMemoryBarrier toTransferDst{};
        toTransferDst.oldLayout = vk::ImageLayout::eUndefined;
        toTransferDst.newLayout = vk::ImageLayout::eTransferDstOptimal;
        toTransferDst.image = dstImage;
        toTransferDst.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        toTransferDst.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        toTransferDst.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                      vk::PipelineStageFlagBits::eTransfer,
                                      {}, {}, {}, toTransferDst);

        vk::ImageMemoryBarrier srcToTransfer{};
        srcToTransfer.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
        srcToTransfer.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        srcToTransfer.image = srcColorImage;
        srcToTransfer.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        srcToTransfer.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        srcToTransfer.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                      vk::PipelineStageFlagBits::eTransfer,
                                      {}, {}, {}, srcToTransfer);

        vk::ImageCopy copyRegion{};
        copyRegion.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        copyRegion.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        copyRegion.extent = vk::Extent3D{renderRes.width, renderRes.height, 1};
        commandBuffer.copyImage(srcColorImage, vk::ImageLayout::eTransferSrcOptimal,
                                dstImage, vk::ImageLayout::eTransferDstOptimal,
                                copyRegion);

        vk::ImageMemoryBarrier dstToRead{};
        dstToRead.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        dstToRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        dstToRead.image = dstImage;
        dstToRead.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        dstToRead.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        dstToRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                      vk::PipelineStageFlagBits::eComputeShader,
                                      {}, {}, {}, dstToRead);

        // Restore the scene color to the layout the render graph expects
        vk::ImageMemoryBarrier srcBack{};
        srcBack.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        srcBack.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
        srcBack.image = srcColorImage;
        srcBack.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        srcBack.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        srcBack.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderRead;
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                      vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader,
                                      {}, {}, {}, srcBack);

        preTransparencyCaptured = true;
    }

    // ======================== Graph-managed dispatch variants ========================

    void RenderPassHandler::drawOverlaysGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        if (billboardPipelineInitialized && !currentBillboardDrawList.empty())
            billboardPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex);

        if (textPipelineInitialized && !currentTextDrawList.empty())
            textPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex);
    }

    void RenderPassHandler::drawUIOverlaysGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        // When upscaling is active the UI composites into displayColorImages (display res), which the
        // frame graph does not track — executePostUpscale leaves it in SHADER_READ_ONLY_OPTIMAL. The UI
        // records begin dynamic rendering with colorLoad (expects COLOR_ATTACHMENT_OPTIMAL), so transition
        // it here. Without upscaling the UI targets the render-res colorImages, which the graph already
        // transitions via the UIOverlays pass's ColorAttachmentWrite declaration — leave that path alone.
        const bool hasUIImages = uiPipelineInitialized && !currentUIImageDrawList.empty();
        const bool hasUIText = uiTextPipelineInitialized && !currentUITextDrawList.empty();
        // Only round-trip the display target's layout when we actually record UI into it.
        // With nothing to draw the transition pair is a no-op that would still assert the
        // image is currently in SHADER_READ_ONLY_OPTIMAL — skip it to avoid a spurious
        // barrier and a layout-mismatch if the post-upscale path left it elsewhere.
        const bool hasDisplay = !offscreenResources.displayColorImages.empty() && (hasUIImages || hasUIText);

        if (hasDisplay)
            core::ImageUtilities::transitionImageLayout(commandBuffer,
                offscreenResources.displayColorImages[imageIndex].colorImage,
                vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);

        if (hasUIImages)
            uiPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex);

        if (hasUIText)
            uiTextPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex);

        // Overlay layer (tooltips, modal windows) records after ALL main UI
        // images and text so its backgrounds cover underlying labels too.
        if (hasUIImages)
            uiPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex, true);

        if (hasUIText)
            uiTextPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex, true);

        // Restore to SHADER_READ_ONLY_OPTIMAL so render() can sample displayColorImages for presentation.
        if (hasDisplay)
            core::ImageUtilities::transitionImageLayout(commandBuffer,
                offscreenResources.displayColorImages[imageIndex].colorImage,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
    }

    bool RenderPassHandler::vfxNeedsPrevFrameDepth() const
    {
        return vfxRuntimeProvider && vfxRuntimeProvider->needsPrevFrameDepth();
    }

    void RenderPassHandler::updateVFXPrevFrameDepth(uint32_t imageIndex)
    {
        if (!vfxRuntimeProvider)
            return;

        // Ramp-up: only expose the real depth once both flight slots have been written by the DepthCopy
        // pass at least once, so the sim never samples an undefined-layout image (fallback is bound until
        // then). Resets automatically when prevFrameDepth is torn down (resize / disable).
        if (offscreenResources.prevFrameDepthCreated)
        {
            if (prevFrameDepthReadyCounter <= core::MAX_FRAMES_IN_FLIGHT)
                ++prevFrameDepthReadyCounter;
        }
        else
        {
            prevFrameDepthReadyCounter = 0;
        }

        const bool depthReady = offscreenResources.prevFrameDepthCreated
            && prevFrameDepthReadyCounter > core::MAX_FRAMES_IN_FLIGHT;

        std::vector<vk::ImageView> slots;
        if (depthReady)
        {
            slots.reserve(core::MAX_FRAMES_IN_FLIGHT);
            for (uint32_t i = 0; i < core::MAX_FRAMES_IN_FLIGHT; ++i)
                slots.push_back(offscreenResources.prevFrameDepth[i].imageView);
        }

        // Read the slot NOT written by this frame's DepthCopy (which writes imageIndex % N) to avoid a
        // same-frame write-after-read; for MAX_FRAMES_IN_FLIGHT this is always a different, already-written
        // slot holding the previous frame's depth (fence-safe by graphics-queue submission order).
        const uint32_t readSlot = (imageIndex + 1u) % core::MAX_FRAMES_IN_FLIGHT;

        vfxRuntimeProvider->setPrevFrameDepth(slots, readSlot, depthReady);
    }

    void RenderPassHandler::drawSceneMeshesGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        bool hasDebugItems = debugRendererInitialized && debugRenderer->hasItemsToRender();
        bool hasVFX = vfxRuntimeProvider && vfxRuntimeProvider->isInitialized()
            && vfxRuntimeProvider->getInstanceCount() > 0;
        bool hasCustomShaderMeshes = !customShaderMeshDrawList.empty();
        bool hasPluginDraws = customPipelineManager && customPipelineManager->hasDraws();

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

        // VFX proxy lights: instances with light emission illuminate the scene
        // as transient point lights (set before dispatchCompute runs
        // updateFromScene; an empty list clears last frame's lights)
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            if (auto* lbm = gpuDrivenRenderer->getLightBufferManager())
            {
                std::vector<lighting::GPULightBufferManager::TransientPointLight> transientLights;
                if (hasVFX)
                {
                    auto proxyLights = vfxRuntimeProvider->getActiveProxyLights();
                    transientLights.reserve(proxyLights.size());
                    for (const auto& proxy : proxyLights)
                        transientLights.push_back({proxy.position, proxy.color, proxy.intensity, proxy.radius});
                }
                lbm->setTransientPointLights(std::move(transientLights));
            }
        }

        bool hasTerrainToRender = gpuDrivenRenderer && gpuDrivenRenderer->isTerrainRenderingEnabled()
            && terrainRenderProvider && terrainRenderProvider->hasActiveTerrain();

        bool hasWaterToRender = gpuDrivenRenderer && gpuDrivenRenderer->isWaterRenderingEnabled()
            && oceanRenderProvider && oceanRenderProvider->hasActiveOcean();

        bool hasBillboardsToRender = gpuDrivenRenderer && gpuDrivenRenderer->isBillboardRenderingEnabled();

        bool needsMeshPass = meshPipelineInitialized && (!currentMeshDrawList.empty() || hasCustomShaderMeshes
            || hasDebugItems || hasVFX || hasTerrainToRender || hasWaterToRender || hasPluginDraws
            || hasBillboardsToRender);

        updateGPUDrivenSceneData();

        if (!needsMeshPass)
        {
            if (decalRenderingEnabled && decalPipeline && decalPipeline->isInitialized() && decalPipeline->hasDecals())
            {
                decalPipeline->setCameraData(currentView, currentProjection, currentNearPlane, currentFarPlane);
                decalPipeline->renderGraphManaged(commandBuffer, imageIndex);
            }
            return;
        }

        DebugRenderer* debugRendererPtr = hasDebugItems ? debugRenderer.get() : nullptr;

        if (hasVFX && !asyncComputeActive)
            vfxRuntimeProvider->recordComputeCommands(commandBuffer);

        bool hasMeshesToRender = !currentMeshDrawList.empty();

        if ((hasMeshesToRender || hasTerrainToRender || hasWaterToRender || hasPluginDraws || hasBillboardsToRender)
            && gpuDrivenRendererInitialized && gpuDrivenRenderer->isEnabled())
        {
            drawGPUDrivenMeshPassGraphManaged(commandBuffer, imageIndex, debugRendererPtr, hasCustomShaderMeshes, hasVFX);
        }
        else if (!currentMeshDrawList.empty() || hasCustomShaderMeshes || hasDebugItems)
        {
            meshPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex, combinedMeshDrawList, currentFrustum,
                                              debugRendererPtr, currentView, currentProjection);
            if (hasVFX)
            {
                capturePreTransparencyColor(commandBuffer, imageIndex);
                meshPipeline->beginVFXRenderPassGraphManaged(commandBuffer, imageIndex);
                vfxRuntimeProvider->recordDrawCommands(commandBuffer);
                meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);
                meshPipeline->restoreDepthAfterVFX(commandBuffer);
            }
        }
        else if (hasVFX)
        {
            meshPipeline->beginRenderPassGraphManaged(commandBuffer, imageIndex);
            meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);

            capturePreTransparencyColor(commandBuffer, imageIndex);
            meshPipeline->beginVFXRenderPassGraphManaged(commandBuffer, imageIndex);
            vfxRuntimeProvider->recordDrawCommands(commandBuffer);
            meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);
            meshPipeline->restoreDepthAfterVFX(commandBuffer);
        }
    }

    void RenderPassHandler::drawGPUDrivenMeshPassGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                                               DebugRenderer* debugRendererPtr, bool hasCustomShaderMeshes,
                                                               bool hasVFX) const
    {
        updateGPUDrivenHiZ();

        // Reset the aux timestamp pool before the earliest scope below — the
        // vkCmdResetQueryPool inside must run outside a render pass, and this point
        // (before any dynamic-rendering pass begins) is the last one that precedes
        // the cull/VSM scope. Same-render-pass scopes measure parse-start → drain,
        // so rows overlapping other GPU work are upper bounds.
        beginVTTimestamps(commandBuffer, imageIndex);

        uint32_t cullScope = vtScopeBegin(commandBuffer, imageIndex, "GPU Cull + VSM Raster");
        if (asyncComputeActive)
            gpuDrivenRenderer->dispatchGraphicsCompute(commandBuffer, imageIndex);
        else
            gpuDrivenRenderer->dispatchCompute(commandBuffer, imageIndex);
        vtScopeEnd(commandBuffer, imageIndex, cullScope);

        vk::DescriptorSet iblDescriptorSet = meshPipeline->getIBLDescriptorSet(imageIndex);

        if (gpuDrivenRenderer->isMeshletOcclusionCullingEnabled())
        {
            uint32_t prepassScope = vtScopeBegin(commandBuffer, imageIndex, "Depth Prepass + HiZ");
            gpuDrivenRenderer->renderDepthPrepass(commandBuffer, iblDescriptorSet);
            gpuDrivenRenderer->generatePrepassHiZ(commandBuffer);
            vtScopeEnd(commandBuffer, imageIndex, prepassScope);
        }

        // Plugin world mask: one-time pipeline recreate on first bind + descriptor upkeep
        gpuDrivenRenderer->dispatchWorldMask();

        if (gpuDrivenRenderer->isRTShadowReady())
            gpuDrivenRenderer->dispatchRTShadow(commandBuffer, imageIndex);

        if (gpuDrivenRenderer->isRTSpotShadowReady())
            gpuDrivenRenderer->dispatchRTSpotShadow(commandBuffer, imageIndex);

        if (gpuDrivenRenderer->isRTPointShadowReady())
            gpuDrivenRenderer->dispatchRTPointShadow(commandBuffer, imageIndex);

        if (oceanFFTInitialized)
        {
            gpuDrivenRenderer->readbackOceanDisplacement();
            gpuDrivenRenderer->dispatchOceanFFT(commandBuffer, currentTime);
        }

        // VK-1480: the raw VT commands below (RVT bake, SVT update, feedback copies) run
        // outside the RenderGraph passes, so bracket them with the aux timestamp pool —
        // the scopes surface as rows in the Task Graph Profiler. All no-ops when the
        // profiler is off. The pool reset happens at the top of this function.

        // VK-1209: bake requested terrain RVT pages into the atlas (dynamic rendering, outside the
        // scene pass) before the terrain draw samples it.
        if (gpuDrivenRenderer->isTerrainRVTActive())
        {
            uint32_t vtBakeScope = vtScopeBegin(commandBuffer, imageIndex, "VT/RVT Bake");
            gpuDrivenRenderer->bakeTerrainRVT(commandBuffer);
            vtScopeEnd(commandBuffer, imageIndex, vtBakeScope);
        }

        // VK-1209: stream + upload requested material SVT pages, and clear feedback, before meshes sample.
        if (gpuDrivenRenderer->isSVTActive())
        {
            uint32_t vtSvtScope = vtScopeBegin(commandBuffer, imageIndex, "VT/SVT Update");
            gpuDrivenRenderer->updateAndUploadSVT(commandBuffer);
            vtScopeEnd(commandBuffer, imageIndex, vtSvtScope);
        }

        // VK-1493: upload any dirtied toon profile rows before the scene meshes sample the
        // set-1 binding-6 table. No-op unless a profile changed (not gated by SVT).
        gpuDrivenRenderer->uploadToonProfiles(commandBuffer);

        bool useParallel = parallelSceneRecording && sceneThreadPoolManager &&
                           sceneThreadPoolManager->getThreadCount() > 1;

        bool wboitActive = wboitEnabled && wboitPipeline && wboitPipeline->isInitialized()
                           && gpuDrivenRenderer->isWBOITReady();

        if (useParallel)
            recordParallelScenePassGraphManaged(commandBuffer, imageIndex, iblDescriptorSet,
                                                debugRendererPtr, hasCustomShaderMeshes, wboitActive);
        else
            recordInlineScenePassGraphManaged(commandBuffer, imageIndex, iblDescriptorSet,
                                              debugRendererPtr, hasCustomShaderMeshes, wboitActive);

        // VK-1480: one aux scope spanning both feedback copies (skipped when neither VT
        // path is active this frame).
        const bool vtFeedbackActive = gpuDrivenRenderer->isTerrainRVTActive() || gpuDrivenRenderer->isSVTActive();
        uint32_t vtFeedbackScope = vtFeedbackActive
            ? vtScopeBegin(commandBuffer, imageIndex, "VT/Feedback Copy")
            : UINT32_MAX;

        // VK-1209: terrain wrote its page requests during the scene pass; copy them to staging for
        // next frame's readback (outside the pass).
        if (gpuDrivenRenderer->isTerrainRVTActive())
            gpuDrivenRenderer->copyTerrainRVTFeedback(commandBuffer);

        // VK-1209: meshes wrote their SVT page requests during the scene pass; copy to staging.
        if (gpuDrivenRenderer->isSVTActive())
            gpuDrivenRenderer->copySVTFeedback(commandBuffer);

        vtScopeEnd(commandBuffer, imageIndex, vtFeedbackScope);
        endVTTimestamps(imageIndex);

        if (decalRenderingEnabled && decalPipeline && decalPipeline->isInitialized() && decalPipeline->hasDecals())
        {
            decalPipeline->setCameraData(currentView, currentProjection, currentNearPlane, currentFarPlane);
            decalPipeline->renderGraphManaged(commandBuffer, imageIndex);
        }

        // Opaque rendering (incl. decals) is complete - snapshot it for the
        // upscaler reactive mask before transparency (WBOIT/VFX) draws
        if ((wboitActive && gpuDrivenRenderer->hasTransparentObjects()) || hasVFX)
            capturePreTransparencyColor(commandBuffer, imageIndex);

        if (wboitActive && gpuDrivenRenderer->hasTransparentObjects())
        {
            wboitPipeline->beginWBOITPass(commandBuffer, imageIndex);
            gpuDrivenRenderer->renderWBOITDraw(commandBuffer, iblDescriptorSet, 0, 0,
                                               hasSelectedEntities());
            wboitPipeline->endWBOITPass(commandBuffer);
            wboitPipeline->composite(commandBuffer, imageIndex);
        }

        if (hasVFX)
        {
            meshPipeline->beginVFXRenderPassGraphManaged(commandBuffer, imageIndex);
            vfxRuntimeProvider->recordDrawCommands(commandBuffer);
            meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);
            meshPipeline->restoreDepthAfterVFX(commandBuffer);
        }
    }

    void RenderPassHandler::recordParallelScenePassGraphManaged(
        const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
        vk::DescriptorSet iblDescriptorSet, DebugRenderer* debugRendererPtr,
        bool hasCustomShaderMeshes, bool wboitActive) const
    {
        auto sceneRecordStart = std::chrono::high_resolution_clock::now();
        sceneThreadPoolManager->resetFrame(imageIndex);

        auto extent = swapChain.getSwapchainExtent();
        vk::Format colorFormat = swapChain.getSceneColorFormat();
        vk::Format depthFormat = swapChain.getSwapchainDepthStencilFormat();

        const vk::SampleCountFlagBits sampleCount = offscreenResources.sampleCount;

        auto setupSecondary = [&](vk::CommandBuffer sec) {
            vk::CommandBufferInheritanceRenderingInfo inheritRendering{};
            inheritRendering.colorAttachmentCount = 1;
            inheritRendering.pColorAttachmentFormats = &colorFormat;
            inheritRendering.depthAttachmentFormat = depthFormat;
            inheritRendering.rasterizationSamples = sampleCount;

            vk::CommandBufferInheritanceInfo inheritance{};
            inheritance.pNext = &inheritRendering;

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
            // Dynamic MSAA: this opaque secondary renders into the (multisampled) scene targets.
            sec.setRasterizationSamplesEXT(sampleCount);
        };

        bool hasTerrain = gpuDrivenRenderer->isTerrainRenderingEnabled();
        bool hasGrass = gpuDrivenRenderer->isGrassRenderingEnabled();
        bool hasWater = gpuDrivenRenderer->isWaterRenderingEnabled();
        bool hasBillboards = gpuDrivenRenderer->isBillboardRenderingEnabled();

        vk::CommandBuffer meshCmd{nullptr};
        vk::CommandBuffer terrainCmd{nullptr};
        vk::CommandBuffer grassCmd{nullptr};
        vk::CommandBuffer waterCmd{nullptr};
        vk::CommandBuffer overlayCmd{nullptr};

        auto meshFuture = threading::JobSystem::instance().submit([&]() {
            meshCmd = sceneThreadPoolManager->getSecondary(0, imageIndex);
            setupSecondary(meshCmd);
            const bool selectionCoverage = hasSelectedEntities();
            gpuDrivenRenderer->renderDraw(meshCmd, iblDescriptorSet, 0, 0, selectionCoverage);
            if (!wboitActive)
                gpuDrivenRenderer->renderTransparentDraw(meshCmd, iblDescriptorSet, 0, 0,
                                                         selectionCoverage);
            gpuDrivenRenderer->renderBlendDraw(meshCmd, iblDescriptorSet, 0, 0, selectionCoverage);
            meshCmd.end();
        }, threading::JobPriority::HIGH);

        std::future<void> terrainFuture;
        uint32_t terrainScope = UINT32_MAX;
        if (hasTerrain)
        {
            // Alloc on the recording thread (cursor/name bookkeeping isn't thread-safe);
            // the job writes the timestamps into its own secondary. Timestamps are legal
            // inside render passes/secondaries — only the pool reset is not.
            terrainScope = vtScopeAlloc(imageIndex, "Terrain Main Draw");
            terrainFuture = threading::JobSystem::instance().submit([&]() {
                terrainCmd = sceneThreadPoolManager->getSecondary(1, imageIndex);
                setupSecondary(terrainCmd);
                vtScopeBeginAt(terrainCmd, imageIndex, terrainScope);
                gpuDrivenRenderer->renderTerrainDraw(terrainCmd, iblDescriptorSet);
                vtScopeEnd(terrainCmd, imageIndex, terrainScope);
                terrainCmd.end();
            }, threading::JobPriority::HIGH);
        }

        std::future<void> grassFuture;
        if (hasGrass)
        {
            grassFuture = threading::JobSystem::instance().submit([&]() {
                grassCmd = sceneThreadPoolManager->getSecondary(2, imageIndex);
                setupSecondary(grassCmd);
                gpuDrivenRenderer->renderGrassDraw(grassCmd);
                grassCmd.end();
            }, threading::JobPriority::HIGH);
        }

        std::future<void> waterFuture;
        if (!hasWater && hasBillboards)
        {
            waterFuture = threading::JobSystem::instance().submit([&]() {
                waterCmd = sceneThreadPoolManager->getSecondary(3, imageIndex);
                setupSecondary(waterCmd);
                gpuDrivenRenderer->renderBillboardDraw(waterCmd, iblDescriptorSet, currentTime);
                waterCmd.end();
            }, threading::JobPriority::HIGH);
        }

        meshFuture.get();
        if (terrainFuture.valid()) terrainFuture.get();
        if (grassFuture.valid()) grassFuture.get();
        if (waterFuture.valid()) waterFuture.get();

        if (!hasWater)
        {
            overlayCmd = sceneThreadPoolManager->getSecondary(4, imageIndex);
            setupSecondary(overlayCmd);
            if (hasCustomShaderMeshes)
                meshPipeline->renderMeshList(overlayCmd, imageIndex, customShaderMeshDrawList, currentFrustum);
            gpuDrivenRenderer->renderGIDebug(overlayCmd, currentProjection * currentView);
            if (debugRendererPtr)
            {
                debugRendererPtr->render(overlayCmd, combinedMeshDrawList, currentView, currentProjection,
                    [this](const std::string& meshId) { return meshPipeline->getMesh(meshId); });
            }
            // Plugin custom pipelines draw last in the scene pass, just before
            // post-processing.
            if (customPipelineManager)
                customPipelineManager->render(overlayCmd, currentView, currentProjection,
                                              buildCustomLightingSets(iblDescriptorSet));
            overlayCmd.end();
        }

        meshPipeline->beginRenderPassForSecondaryGraphManaged(commandBuffer, imageIndex);

        std::vector<vk::CommandBuffer> secondaries;
        secondaries.reserve(5);
        secondaries.push_back(meshCmd);
        if (terrainCmd) secondaries.push_back(terrainCmd);
        if (grassCmd) secondaries.push_back(grassCmd);
        if (!hasWater && waterCmd) secondaries.push_back(waterCmd);
        if (!hasWater && overlayCmd) secondaries.push_back(overlayCmd);

        commandBuffer.executeCommands(
            static_cast<uint32_t>(secondaries.size()),
            secondaries.data()
        );

        if (hasWater)
        {
            meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);

            auto extent = swapChain.getSwapchainExtent();
            gpuDrivenRenderer->copySceneColorForRefraction(
                commandBuffer,
                offscreenResources.colorImages[imageIndex].colorImage,
                extent.width, extent.height);

            // VK-1604: water reads the scene depth image (set 9 b1), so it draws in its own
            // scope with depth bound read-only. Everything after it may write depth, hence the
            // restore + re-begin below.
            meshPipeline->beginWaterReadOnlyDepthPassGraphManaged(commandBuffer, imageIndex);

            vk::Viewport viewport{0.0f, 0.0f,
                                   static_cast<float>(extent.width),
                                   static_cast<float>(extent.height),
                                   0.0f, 1.0f};
            commandBuffer.setViewport(0, viewport);
            vk::Rect2D scissor{{0, 0}, extent};
            commandBuffer.setScissor(0, scissor);

            gpuDrivenRenderer->renderWaterDraw(commandBuffer, iblDescriptorSet);

            meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);
            meshPipeline->restoreDepthAfterWater(commandBuffer);
            meshPipeline->beginWaterContinuePassGraphManaged(commandBuffer, imageIndex);
            commandBuffer.setViewport(0, viewport);
            commandBuffer.setScissor(0, scissor);

            if (hasBillboards)
                gpuDrivenRenderer->renderBillboardDraw(commandBuffer, iblDescriptorSet, currentTime);

            if (hasCustomShaderMeshes)
                meshPipeline->renderMeshList(commandBuffer, imageIndex, customShaderMeshDrawList, currentFrustum);

            gpuDrivenRenderer->renderGIDebug(commandBuffer, currentProjection * currentView);

            if (debugRendererPtr)
            {
                debugRendererPtr->render(commandBuffer, combinedMeshDrawList, currentView, currentProjection,
                    [this](const std::string& meshId) { return meshPipeline->getMesh(meshId); });
            }

            // Plugin custom pipelines draw last in the scene pass, just before
            // post-processing.
            if (customPipelineManager)
                customPipelineManager->render(commandBuffer, currentView, currentProjection,
                                              buildCustomLightingSets(iblDescriptorSet));
        }

        auto sceneRecordEnd = std::chrono::high_resolution_clock::now();
        lastSceneRecordingUs = std::chrono::duration<float, std::micro>(sceneRecordEnd - sceneRecordStart).count();
        lastSceneSecondaryCount = static_cast<uint32_t>(secondaries.size());

        meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);
    }

    void RenderPassHandler::recordInlineScenePassGraphManaged(
        const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
        vk::DescriptorSet iblDescriptorSet, DebugRenderer* debugRendererPtr,
        bool hasCustomShaderMeshes, bool wboitActive) const
    {
        meshPipeline->beginRenderPassGraphManaged(commandBuffer, imageIndex);

        auto extent = swapChain.getSwapchainExtent();
        vk::Viewport viewport{0.0f, 0.0f,
                               static_cast<float>(extent.width), static_cast<float>(extent.height),
                               0.0f, 1.0f};
        commandBuffer.setViewport(0, viewport);
        vk::Rect2D scissor{{0, 0}, extent};
        commandBuffer.setScissor(0, scissor);

        const bool selectionCoverage = hasSelectedEntities();
        gpuDrivenRenderer->renderDraw(commandBuffer, iblDescriptorSet, 0, 0, selectionCoverage);

        if (!wboitActive)
            gpuDrivenRenderer->renderTransparentDraw(commandBuffer, iblDescriptorSet, 0, 0,
                                                     selectionCoverage);

        gpuDrivenRenderer->renderBlendDraw(commandBuffer, iblDescriptorSet, 0, 0,
                                           selectionCoverage);

        if (gpuDrivenRenderer->isTerrainRenderingEnabled())
        {
            uint32_t terrainScope = vtScopeBegin(commandBuffer, imageIndex, "Terrain Main Draw");
            gpuDrivenRenderer->renderTerrainDraw(commandBuffer, iblDescriptorSet);
            vtScopeEnd(commandBuffer, imageIndex, terrainScope);
        }

        if (gpuDrivenRenderer->isGrassRenderingEnabled())
            gpuDrivenRenderer->renderGrassDraw(commandBuffer);

        bool hasWater = gpuDrivenRenderer->isWaterRenderingEnabled()
            && oceanRenderProvider && oceanRenderProvider->hasActiveOcean();

        if (hasWater)
        {
            meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);

            gpuDrivenRenderer->copySceneColorForRefraction(
                commandBuffer,
                offscreenResources.colorImages[imageIndex].colorImage,
                extent.width, extent.height);

            // VK-1604: water reads the scene depth image (set 9 b1), so it draws in its own
            // scope with depth bound read-only, then depth is restored to AttachmentOptimal for
            // the billboards / custom meshes / debug / plugin draws that follow.
            meshPipeline->beginWaterReadOnlyDepthPassGraphManaged(commandBuffer, imageIndex);

            auto waterExtent = swapChain.getSwapchainExtent();
            vk::Viewport waterViewport{0.0f, 0.0f,
                                        static_cast<float>(waterExtent.width),
                                        static_cast<float>(waterExtent.height),
                                        0.0f, 1.0f};
            commandBuffer.setViewport(0, waterViewport);
            vk::Rect2D waterScissor{{0, 0}, waterExtent};
            commandBuffer.setScissor(0, waterScissor);

            gpuDrivenRenderer->renderWaterDraw(commandBuffer, iblDescriptorSet);

            meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);
            meshPipeline->restoreDepthAfterWater(commandBuffer);
            meshPipeline->beginWaterContinuePassGraphManaged(commandBuffer, imageIndex);
            commandBuffer.setViewport(0, waterViewport);
            commandBuffer.setScissor(0, waterScissor);
        }

        if (gpuDrivenRenderer->isBillboardRenderingEnabled())
            gpuDrivenRenderer->renderBillboardDraw(commandBuffer, iblDescriptorSet, currentTime);

        if (hasCustomShaderMeshes)
            meshPipeline->renderMeshList(commandBuffer, imageIndex, customShaderMeshDrawList, currentFrustum);

        gpuDrivenRenderer->renderGIDebug(commandBuffer, currentProjection * currentView);

        if (debugRendererPtr)
        {
            debugRendererPtr->render(commandBuffer, combinedMeshDrawList, currentView, currentProjection,
                                     [this](const std::string& meshId)
                                     {
                                         return meshPipeline->getMesh(meshId);
                                     });
        }

        // Plugin custom pipelines draw last in the scene pass, just before
        // post-processing.
        if (customPipelineManager)
            customPipelineManager->render(commandBuffer, currentView, currentProjection,
                                          buildCustomLightingSets(iblDescriptorSet));

        meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);
    }
}
