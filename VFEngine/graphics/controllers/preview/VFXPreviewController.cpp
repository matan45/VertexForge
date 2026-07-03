#include "VFXPreviewController.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/CommandPool.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/RenderManager.hpp"
#include "../../core/VulkanContext.hpp"
#include "../../render/vfx/billboard/VFXBillboardPipeline.hpp"
#include "../../render/vfx/mesh/VFXMeshPreviewPipeline.hpp"
#include "../../render/vfx/ribbon/VFXRibbonPreviewPipeline.hpp"
#include "../../render/vfx/particle/VFXParticleSystem.hpp"
#include "../../render/mesh/MeshGPUCache.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "vfx/VFXModifierConfigLoader.hpp"
#include "print/Log.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <imgui_impl_vulkan.h>

namespace controllers
{
    namespace
    {
        render::vfx::VFXEmitterConfig buildEmitterConfig(const VFXPreviewParams& params)
        {
            render::vfx::VFXEmitterConfig config;
            config.spawnRate = params.spawnRate;
            config.lifetime = params.lifetime;
            config.startSize = params.startSize;
            config.startSpeed = params.startSpeed;
            config.startColor = params.startColor;
            config.emitDirection = params.emitDirection;
            config.texturePath = params.texturePath;
            config.looping = params.looping;
            config.sizeVariance = params.sizeVariance;
            config.lifetimeVariance = params.lifetimeVariance;
            config.speedVariance = params.speedVariance;
            config.rotationVariance = params.rotationVariance;
            config.angularVelocityVariance = params.angularVelocityVariance;
            config.colorValueVariance = params.colorValueVariance;
            config.alphaVariance = params.alphaVariance;
            config.modifiers = params.modifiers;
            config.forces = params.forces;
            config.shape = params.shape;
            config.bursts = params.bursts;
            config.flipbookRows = params.flipbookRows;
            config.flipbookColumns = params.flipbookColumns;
            config.flipbookFrameRate = params.flipbookFrameRate;
            config.flipbookRandomStart = params.flipbookRandomStart;
            config.flipbookFrameBlend = params.flipbookFrameBlend;
            config.alphaClipThreshold = params.alphaClipThreshold;
            config.additiveBlend = params.additiveBlend;
            config.renderMode = static_cast<render::vfx::VFXRenderMode>(params.renderMode);
            config.softParticleDistance = params.softParticleDistance;
            config.stretchMultiplier = params.stretchMultiplier;
            config.meshPath = params.meshPath;
            config.maxTrailPoints = static_cast<uint32_t>(params.maxTrailPoints);
            config.ribbonWidth = params.ribbonWidth;
            config.ribbonMinDistance = params.ribbonMinDistance;
            config.uvScrollSpeedU = params.uvScrollSpeedU;
            config.uvScrollSpeedV = params.uvScrollSpeedV;
            config.events = params.events;
            config.collisionEnabled = params.collisionEnabled;
            config.collisionBounce = params.collisionBounce;
            config.collisionFriction = params.collisionFriction;
            config.collisionLifetimeLoss = params.collisionLifetimeLoss;
            config.lightingInfluence = params.lightingInfluence;
            config.normalMode = params.normalMode;
            config.ambientAmount = params.ambientAmount;
            return config;
        }
    }

    VFXPreviewController::VFXPreviewController()
        : swapChain{*core::VulkanContext::getSwapChain()}
        , device{*core::VulkanContext::getDevice()}
        , commandPool{std::make_unique<core::CommandPool>(device, swapChain)}
        , particleSystem{std::make_unique<render::vfx::VFXParticleSystem>()}
    {
    }

    VFXPreviewController::~VFXPreviewController()
    {
        cleanUp();
    }

    void VFXPreviewController::init()
    {
        if (initialized)
        {
            return;
        }

        createSampler();
        createOffscreenResources();

        vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};
        inFlightFences.resize(swapChain.getImageCount());
        for (auto& fence : inFlightFences)
        {
            fence = device.getLogicalDevice().createFence(fenceInfo);
        }

        pipeline = std::make_unique<render::vfx::VFXBillboardPipeline>(device, swapChain, offscreenResources);
        pipeline->init();
        
        previewMeshCache = std::make_unique<render::mesh::MeshGPUCache>(device);
        meshPipeline = std::make_unique<render::vfx::VFXMeshPreviewPipeline>(device, swapChain, offscreenResources, *previewMeshCache);
        meshPipeline->init();
        
        ribbonPipeline = std::make_unique<render::vfx::VFXRibbonPreviewPipeline>(device, swapChain, offscreenResources);
        ribbonPipeline->init();

        render::vfx::VFXEmitterConfig config = buildEmitterConfig(currentParams);
        particleSystem->setEmitterConfig(config);

        if (!currentParams.texturePath.empty())
        {
            pipeline->setTexture(currentParams.texturePath);
        }
        render::vfx::VFXFlipbookConfig fbConfig;
        fbConfig.rows = currentParams.flipbookRows;
        fbConfig.columns = currentParams.flipbookColumns;
        fbConfig.alphaClipThreshold = currentParams.alphaClipThreshold;
        fbConfig.additiveBlend = currentParams.additiveBlend;
        fbConfig.renderMode = static_cast<render::vfx::VFXRenderMode>(currentParams.renderMode);
        fbConfig.stretchMultiplier = currentParams.stretchMultiplier;
        fbConfig.glowColor = ::vfx::VFXModifierConfigLoader::getGlowColorFromChain(currentParams.modifiers);
        fbConfig.uvScrollSpeedU = currentParams.uvScrollSpeedU;
        fbConfig.uvScrollSpeedV = currentParams.uvScrollSpeedV;
        fbConfig.frameBlend = currentParams.flipbookFrameBlend;
        fbConfig.frameRate = currentParams.flipbookFrameRate;
        pipeline->setFlipbookConfig(fbConfig);

        if (!currentParams.meshPath.empty())
        {
            meshPipeline->setMesh(currentParams.meshPath);
        }
        if (!currentParams.texturePath.empty())
        {
            meshPipeline->setTexture(currentParams.texturePath);
        }
        meshPipeline->setRenderingConfig(currentParams.alphaClipThreshold, currentParams.additiveBlend, fbConfig.glowColor,
                                          currentParams.uvScrollSpeedU, currentParams.uvScrollSpeedV);

        if (!currentParams.texturePath.empty())
        {
            ribbonPipeline->setTexture(currentParams.texturePath);
        }
        ribbonPipeline->setRenderingConfig(currentParams.alphaClipThreshold, currentParams.additiveBlend,
                                            currentParams.ribbonWidth, fbConfig.glowColor,
                                            currentParams.uvScrollSpeedU, currentParams.uvScrollSpeedV);

        lastExtent = swapChain.getSwapchainExtent();
        initialized = true;
    }

    void VFXPreviewController::cleanUp()
    {
        if (!initialized)
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        clearBundles(); // VK-1451: destroy per-step pipelines before the shared resources

        if (ribbonPipeline)
        {
            ribbonPipeline->cleanUp();
            ribbonPipeline.reset();
        }

        if (meshPipeline)
        {
            meshPipeline->cleanUp();
            meshPipeline.reset();
        }

        if (previewMeshCache)
        {
            previewMeshCache->unloadAllMeshes();
            previewMeshCache.reset();
        }

        if (pipeline)
        {
            pipeline->cleanUp();
            pipeline.reset();
        }

        commandPool->cleanUp();

        for (auto& fence : inFlightFences)
        {
            if (fence)
            {
                device.getLogicalDevice().destroyFence(fence);
            }
        }
        inFlightFences.clear();

        for (auto const& resources : offscreenResources.colorImages)
        {
            if (resources.descriptorSet)
            {
                ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
            }
        }

        if (sampler)
        {
            device.getLogicalDevice().destroySampler(sampler);
            sampler = nullptr;
        }

        cleanupOffscreenResources();

        initialized = false;
    }

    void VFXPreviewController::setParams(const VFXPreviewParams& params)
    {
        currentParams = params;

        if (particleSystem)
        {
            render::vfx::VFXEmitterConfig config = buildEmitterConfig(params);
            particleSystem->setEmitterConfig(config);
        }

        render::vfx::VFXFlipbookConfig fbConfig;
        fbConfig.rows = params.flipbookRows;
        fbConfig.columns = params.flipbookColumns;
        fbConfig.alphaClipThreshold = params.alphaClipThreshold;
        fbConfig.additiveBlend = params.additiveBlend;
        fbConfig.renderMode = static_cast<render::vfx::VFXRenderMode>(params.renderMode);
        fbConfig.stretchMultiplier = params.stretchMultiplier;
        fbConfig.glowColor = ::vfx::VFXModifierConfigLoader::getGlowColorFromChain(params.modifiers);
        fbConfig.uvScrollSpeedU = params.uvScrollSpeedU;
        fbConfig.uvScrollSpeedV = params.uvScrollSpeedV;
        fbConfig.frameBlend = params.flipbookFrameBlend;
        fbConfig.frameRate = params.flipbookFrameRate;

        if (pipeline && pipeline->isInitialized())
        {
            pipeline->setTexture(params.texturePath);
            pipeline->setFlipbookConfig(fbConfig);
        }
        
        if (meshPipeline && meshPipeline->isInitialized())
        {
            meshPipeline->setMesh(params.meshPath);
            meshPipeline->setTexture(params.texturePath);
            meshPipeline->setRenderingConfig(params.alphaClipThreshold, params.additiveBlend, fbConfig.glowColor,
                                              params.uvScrollSpeedU, params.uvScrollSpeedV);
        }

        if (ribbonPipeline && ribbonPipeline->isInitialized())
        {
            ribbonPipeline->setTexture(params.texturePath);
            ribbonPipeline->setRenderingConfig(params.alphaClipThreshold, params.additiveBlend,
                                                params.ribbonWidth, fbConfig.glowColor,
                                                params.uvScrollSpeedU, params.uvScrollSpeedV);
        }
    }

    void VFXPreviewController::updateCamera(const glm::mat4& view, const glm::mat4& projection,
                                             const glm::vec3& cameraPos, float time)
    {
        lastView = view;
        lastProjection = projection;
        lastCameraPos = cameraPos;
        lastCameraTime = time;

        if (pipeline && pipeline->isInitialized())
        {
            pipeline->updateCameraUBO(view, projection, cameraPos, time);
        }

        if (meshPipeline && meshPipeline->isInitialized())
        {
            meshPipeline->updateCameraUBO(view, projection, cameraPos, time);
        }

        if (ribbonPipeline && ribbonPipeline->isInitialized())
        {
            ribbonPipeline->updateCameraUBO(view, projection, cameraPos, time);
        }

        // Sequence mode: every step bundle owns its own pipeline and camera UBO.
        for (auto& bundle : bundles)
        {
            if (bundle.billboard && bundle.billboard->isInitialized())
                bundle.billboard->updateCameraUBO(view, projection, cameraPos, time);
            if (bundle.mesh && bundle.mesh->isInitialized())
                bundle.mesh->updateCameraUBO(view, projection, cameraPos, time);
            if (bundle.ribbon && bundle.ribbon->isInitialized())
                bundle.ribbon->updateCameraUBO(view, projection, cameraPos, time);
        }
    }

    void VFXPreviewController::update(float deltaTime)
    {
        if (sequenceMode)
        {
            const float scaled = sequencePlaying ? deltaTime * sequenceRate : 0.0f;
            if (sequenceFixedStep > 0.0f)
            {
                sequenceAccumulator += scaled;
                int guard = 0;
                while (sequenceAccumulator >= sequenceFixedStep && guard < 4096)
                {
                    stepSequence(sequenceFixedStep);
                    sequenceAccumulator -= sequenceFixedStep;
                    ++guard;
                }
            }
            else if (scaled > 0.0f)
            {
                stepSequence(scaled);
            }
            return;
        }

        if (particleSystem)
        {
            particleSystem->update(deltaTime);
        }
    }

    void VFXPreviewController::play()
    {
        if (sequenceMode)
        {
            sequencePlaying = true;
            return;
        }
        if (particleSystem)
        {
            particleSystem->setPlaying(true);
        }
    }

    void VFXPreviewController::pause()
    {
        if (sequenceMode)
        {
            sequencePlaying = false;
            return;
        }
        if (particleSystem)
        {
            particleSystem->setPlaying(false);
        }
    }

    void VFXPreviewController::stop()
    {
        if (sequenceMode)
        {
            sequencePlaying = false;
            seekSequence(0.0f);
            return;
        }
        if (particleSystem)
        {
            particleSystem->setPlaying(false);
            particleSystem->reset();
        }
    }

    bool VFXPreviewController::isPlaying() const
    {
        if (sequenceMode)
            return sequencePlaying;
        return particleSystem ? particleSystem->isPlaying() : false;
    }

    void VFXPreviewController::recreateOffscreenResources()
    {
        device.getLogicalDevice().waitIdle();

        // Remove old ImGui descriptor sets before destroying image views
        for (auto const& resources : offscreenResources.colorImages)
        {
            if (resources.descriptorSet)
            {
                ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
            }
        }

        cleanupOffscreenResources();
        createOffscreenResources();

        pipeline->recreate();

        if (meshPipeline)
        {
            meshPipeline->recreate();
        }

        if (ribbonPipeline)
        {
            ribbonPipeline->recreate();
        }

        lastExtent = swapChain.getSwapchainExtent();
    }

    void* VFXPreviewController::render()
    {
        if (!initialized || !pipeline || !pipeline->isInitialized())
        {
            return nullptr;
        }

        vk::Extent2D currentExtent = swapChain.getSwapchainExtent();
        if (currentExtent.width != lastExtent.width || currentExtent.height != lastExtent.height)
        {
            recreateOffscreenResources();
        }

        uint32_t imageIndex = core::RenderManager::getImageIndex();

        vk::Result result = device.getLogicalDevice().waitForFences(
            1, &inFlightFences[imageIndex], VK_TRUE, UINT64_MAX);
        result = device.getLogicalDevice().resetFences(1, &inFlightFences[imageIndex]);
        (void)result;

        vk::CommandBuffer commandBuffer = commandPool->getCommandBuffer(imageIndex);
        commandBuffer.reset();
        commandBuffer.begin(vk::CommandBufferBeginInfo{});

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        if (sequenceMode)
        {
            // One render pass, one clear, every live step bundle composited on top.
            auto colorAttach = core::colorClear(
                offscreenResources.colorImages[imageIndex].colorImageView,
                vk::ClearColorValue(std::array<float, 4>{0.1f, 0.1f, 0.1f, 1.0f}));
            auto depthAttach = core::depthClear(offscreenResources.depthImage.depthImageView, 1.0f, 0);

            core::DynamicRenderingInfo dynInfo{};
            dynInfo.extent = swapChain.getSwapchainExtent();
            dynInfo.colorAttachments = {colorAttach};
            dynInfo.depthAttachment = depthAttach;

            core::beginDynamicRendering(commandBuffer, dynInfo);
            for (auto& bundle : bundles)
            {
                recordBundle(bundle, commandBuffer);
            }
            core::endDynamicRendering(commandBuffer);
        }
        else
        {
            // Single-emitter path: route to the pipeline for the active render mode.
            auto renderMode = static_cast<render::vfx::VFXRenderMode>(currentParams.renderMode);
            bool useMeshPipeline = renderMode == render::vfx::VFXRenderMode::MeshParticle &&
                                   meshPipeline && meshPipeline->isInitialized() &&
                                   meshPipeline->hasMesh();
            bool useRibbonPipeline = renderMode == render::vfx::VFXRenderMode::Ribbon &&
                                     ribbonPipeline && ribbonPipeline->isInitialized();

            if (particleSystem)
            {
                if (useRibbonPipeline)
                {
                    const auto& segments = particleSystem->getRibbonSegments();
                    ribbonPipeline->setRibbonSegments(segments);
                }
                else
                {
                    auto instances = particleSystem->getInstanceData();
                    if (useMeshPipeline)
                        meshPipeline->setParticleInstances(instances);
                    else
                        pipeline->setParticleInstances(instances);
                }
            }

            if (useRibbonPipeline)
                ribbonPipeline->recordCommandBuffer(commandBuffer, imageIndex);
            else if (useMeshPipeline)
                meshPipeline->recordCommandBuffer(commandBuffer, imageIndex);
            else
                pipeline->recordCommandBuffer(commandBuffer, imageIndex);
        }

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        commandBuffer.end();

        vk::SubmitInfo submitInfo(
            0, nullptr, nullptr,
            1, &commandBuffer,
            0, nullptr
        );

        device.submitGraphics(submitInfo, inFlightFences[imageIndex]);

        return static_cast<void*>(offscreenResources.colorImages[imageIndex].descriptorSet);
    }

    // ============================================================
    // VK-1451 — composited sequence preview
    // ============================================================

    void VFXPreviewController::clearBundles()
    {
        if (!bundles.empty())
        {
            device.getLogicalDevice().waitIdle();
            for (auto& bundle : bundles)
            {
                if (bundle.billboard) bundle.billboard->cleanUp();
                if (bundle.mesh) bundle.mesh->cleanUp();
                if (bundle.ribbon) bundle.ribbon->cleanUp();
                if (bundle.meshCache) bundle.meshCache->unloadAllMeshes();
            }
            bundles.clear();
        }
    }

    void VFXPreviewController::setSequence(const VFXSequencePreviewDesc& desc)
    {
        if (!initialized)
            return;

        clearBundles();

        sequenceMode = true;
        sequenceSteps = desc.steps;
        sequenceRate = desc.playbackRate > 0.0f ? desc.playbackRate : 1.0f;
        sequenceFixedStep = desc.fixedStep;
        sequenceAccumulator = 0.0f;
        sequencePlaying = true;

        // Build a scheduling-only VFXSequenceData the timeline can drive (the heavy
        // emitter params live in sequenceSteps and are only consulted when a bundle spawns).
        scheduleData = vfx::VFXSequenceData{};
        scheduleData.seed = desc.seed;
        scheduleData.playbackRate = sequenceRate;
        scheduleData.fixedStep = sequenceFixedStep;
        scheduleData.steps.reserve(desc.steps.size());
        for (const auto& step : desc.steps)
        {
            vfx::VFXSequenceStep s;
            s.startTime = step.startTime;
            s.duration = step.duration;
            s.loop = step.loop;
            s.cueName = step.cueName;
            s.stopMode = step.stopMode == 1 ? vfx::VFXStepStopMode::StopAfterDuration
                                            : vfx::VFXStepStopMode::PlayToCompletion;
            scheduleData.steps.push_back(std::move(s));
        }
        for (const auto& marker : desc.markers)
            scheduleData.eventMarkers.push_back(vfx::VFXSequenceEventMarker{marker.first, marker.second});

        const uint32_t effectiveSeed = desc.seed != 0 ? desc.seed : 1u;
        timeline.reset(scheduleData, effectiveSeed);

        // Spawn whatever is already live at t=0 so the first frame shows something.
        seekSequence(0.0f);
    }

    void VFXPreviewController::setSequenceRate(float rate)
    {
        if (rate > 0.0f)
            sequenceRate = rate;
    }

    void VFXPreviewController::configureSystemFromParams(render::vfx::VFXParticleSystem& system,
                                                         const VFXPreviewParams& params) const
    {
        render::vfx::VFXEmitterConfig config = buildEmitterConfig(params);
        system.setEmitterConfig(config);
    }

    void VFXPreviewController::createBundle(int stepIndex)
    {
        if (stepIndex < 0 || stepIndex >= static_cast<int>(sequenceSteps.size()))
            return;

        const VFXSequencePreviewStep& step = sequenceSteps[static_cast<size_t>(stepIndex)];
        const VFXPreviewParams& params = step.params;

        StepBundle bundle;
        bundle.stepIndex = stepIndex;
        bundle.localTransform = step.localTransform;
        bundle.mode = params.renderMode;
        const auto renderMode = static_cast<render::vfx::VFXRenderMode>(params.renderMode);

        bundle.system = std::make_unique<render::vfx::VFXParticleSystem>();
        bundle.system->setSeed(step.seed != 0 ? step.seed : vfx::VFXComboTimeline::deriveSeed(timeline.seed(), stepIndex));
        bundle.system->reset();
        configureSystemFromParams(*bundle.system, params);
        bundle.system->setPlaying(true);

        render::vfx::VFXFlipbookConfig fbConfig;
        fbConfig.rows = params.flipbookRows;
        fbConfig.columns = params.flipbookColumns;
        fbConfig.alphaClipThreshold = params.alphaClipThreshold;
        fbConfig.additiveBlend = params.additiveBlend;
        fbConfig.renderMode = static_cast<render::vfx::VFXRenderMode>(params.renderMode);
        fbConfig.stretchMultiplier = params.stretchMultiplier;
        fbConfig.glowColor = ::vfx::VFXModifierConfigLoader::getGlowColorFromChain(params.modifiers);
        fbConfig.uvScrollSpeedU = params.uvScrollSpeedU;
        fbConfig.uvScrollSpeedV = params.uvScrollSpeedV;
        fbConfig.frameBlend = params.flipbookFrameBlend;
        fbConfig.frameRate = params.flipbookFrameRate;

        if (renderMode == render::vfx::VFXRenderMode::Ribbon)
        {
            bundle.ribbon = std::make_unique<render::vfx::VFXRibbonPreviewPipeline>(device, swapChain, offscreenResources);
            bundle.ribbon->init();
            if (!params.texturePath.empty())
                bundle.ribbon->setTexture(params.texturePath);
            bundle.ribbon->setRenderingConfig(params.alphaClipThreshold, params.additiveBlend,
                                               params.ribbonWidth, fbConfig.glowColor,
                                               params.uvScrollSpeedU, params.uvScrollSpeedV);
            bundle.ribbon->updateCameraUBO(lastView, lastProjection, lastCameraPos, lastCameraTime);
        }
        else if (renderMode == render::vfx::VFXRenderMode::MeshParticle)
        {
            bundle.meshCache = std::make_unique<render::mesh::MeshGPUCache>(device);
            bundle.mesh = std::make_unique<render::vfx::VFXMeshPreviewPipeline>(device, swapChain, offscreenResources,
                                                                                *bundle.meshCache);
            bundle.mesh->init();
            if (!params.meshPath.empty())
                bundle.mesh->setMesh(params.meshPath);
            if (!params.texturePath.empty())
                bundle.mesh->setTexture(params.texturePath);
            bundle.mesh->setRenderingConfig(params.alphaClipThreshold, params.additiveBlend, fbConfig.glowColor,
                                            params.uvScrollSpeedU, params.uvScrollSpeedV);
            bundle.mesh->updateCameraUBO(lastView, lastProjection, lastCameraPos, lastCameraTime);
        }
        else
        {
            bundle.billboard = std::make_unique<render::vfx::VFXBillboardPipeline>(device, swapChain, offscreenResources);
            bundle.billboard->init();
            if (!params.texturePath.empty())
                bundle.billboard->setTexture(params.texturePath);
            bundle.billboard->setFlipbookConfig(fbConfig);
            bundle.billboard->updateCameraUBO(lastView, lastProjection, lastCameraPos, lastCameraTime);
        }

        bundles.push_back(std::move(bundle));
    }

    void VFXPreviewController::stepSequence(float dt)
    {
        std::vector<vfx::ComboEvent> events;
        timeline.advance(dt, events);
        for (const auto& ev : events)
        {
            if (ev.kind == vfx::ComboEventKind::SpawnStep)
            {
                createBundle(ev.stepIndex);
            }
            else // StopStep: stop emitting; particles already in flight fade out.
            {
                for (auto& b : bundles)
                    if (b.stepIndex == ev.stepIndex && b.system)
                        b.system->setPlaying(false);
            }
        }

        // Advance every live bundle by the same dt so newly-spawned systems age with the
        // exact fixed cadence (deterministic replay).
        for (auto& b : bundles)
            if (b.system)
                b.system->update(dt);
    }

    void VFXPreviewController::seekSequence(float seconds)
    {
        if (!sequenceMode)
            return;

        clearBundles();
        timeline.rewind();
        sequenceAccumulator = 0.0f;

        const float target = std::max(0.0f, seconds);

        // Spawn anything scheduled at t=0 first (advance(0) is allowed and idempotent),
        // then replay forward in whole fixed steps so the spawn set and particle ages
        // match a real playthrough to `target`.
        stepSequence(0.0f);

        if (sequenceFixedStep > 0.0f)
        {
            const long steps = static_cast<long>(std::floor(target / sequenceFixedStep));
            for (long k = 0; k < steps && k < 1000000; ++k)
                stepSequence(sequenceFixedStep);
            sequenceAccumulator = std::max(0.0f, target - static_cast<float>(steps) * sequenceFixedStep);
        }
        else if (target > 0.0f)
        {
            stepSequence(target);
        }
    }

    void VFXPreviewController::recordBundle(const StepBundle& bundle, vk::CommandBuffer commandBuffer) const
    {
        if (!bundle.system)
            return;

        const auto mode = static_cast<render::vfx::VFXRenderMode>(bundle.mode);

        if (mode == render::vfx::VFXRenderMode::Ribbon && bundle.ribbon && bundle.ribbon->isInitialized())
        {
            auto segments = bundle.system->getRibbonSegments();
            for (auto& s : segments)
            {
                s.posA = glm::vec3(bundle.localTransform * glm::vec4(s.posA, 1.0f));
                s.posB = glm::vec3(bundle.localTransform * glm::vec4(s.posB, 1.0f));
            }
            bundle.ribbon->setRibbonSegments(segments);
            bundle.ribbon->recordDraws(commandBuffer);
        }
        else if (mode == render::vfx::VFXRenderMode::MeshParticle && bundle.mesh &&
                 bundle.mesh->isInitialized() && bundle.mesh->hasMesh())
        {
            auto instances = bundle.system->getInstanceData();
            for (auto& inst : instances)
                inst.worldPosition = glm::vec3(bundle.localTransform * glm::vec4(inst.worldPosition, 1.0f));
            bundle.mesh->setParticleInstances(instances);
            bundle.mesh->recordDraws(commandBuffer);
        }
        else if (bundle.billboard && bundle.billboard->isInitialized())
        {
            auto instances = bundle.system->getInstanceData();
            for (auto& inst : instances)
                inst.worldPosition = glm::vec3(bundle.localTransform * glm::vec4(inst.worldPosition, 1.0f));
            bundle.billboard->setParticleInstances(instances);
            bundle.billboard->recordDraws(commandBuffer);
        }
    }

    void VFXPreviewController::createOffscreenResources()
    {
        uint32_t imageCount = swapChain.getImageCount();
        offscreenResources.colorImages.resize(imageCount);

        vk::Extent2D extent = swapChain.getSwapchainExtent();

        for (uint32_t i = 0; i < imageCount; i++)
        {
            core::ImageInfoRequest imageInfo(device.getLogicalDevice(), device.getPhysicalDevice());
            imageInfo.width = extent.width;
            imageInfo.height = extent.height;
            imageInfo.format = swapChain.getSceneColorFormat();
            imageInfo.tiling = vk::ImageTiling::eOptimal;
            imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
            imageInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::ImageUtilities::createImage(imageInfo, offscreenResources.colorImages[i].colorImage,
                                               offscreenResources.colorImages[i].colorImageAllocation, device.getMemoryManager());

            core::ImageViewInfoRequest viewInfo(device.getLogicalDevice(),
                                                 offscreenResources.colorImages[i].colorImage);
            viewInfo.format = swapChain.getSceneColorFormat();
            core::ImageUtilities::createImageView(viewInfo, offscreenResources.colorImages[i].colorImageView);

            vk::UniqueCommandBuffer transitionColorImage = core::Utilities::beginSingleTimeCommands(
                device.getLogicalDevice(), commandPool->getCommandPool());
            core::ImageUtilities::transitionImageLayout(transitionColorImage.get(),
                offscreenResources.colorImages[i].colorImage,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
            core::Utilities::endSingleTimeCommands(device, transitionColorImage);

            updateDescriptorSets(offscreenResources.colorImages[i].descriptorSet,
                                 offscreenResources.colorImages[i].colorImageView);
        }

        core::ImageInfoRequest depthInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        depthInfo.width = extent.width;
        depthInfo.height = extent.height;
        depthInfo.format = swapChain.getSwapchainDepthStencilFormat();
        depthInfo.tiling = vk::ImageTiling::eOptimal;
        depthInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
        depthInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::ImageUtilities::createImage(depthInfo, offscreenResources.depthImage.depthImage,
                                           offscreenResources.depthImage.depthImageAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest depthViewInfo(device.getLogicalDevice(),
                                                  offscreenResources.depthImage.depthImage);
        depthViewInfo.format = swapChain.getSwapchainDepthStencilFormat();
        depthViewInfo.aspectFlags = vk::ImageAspectFlagBits::eDepth;
        core::ImageUtilities::createImageView(depthViewInfo, offscreenResources.depthImage.depthImageView);

        vk::UniqueCommandBuffer transitionDepthImage = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool->getCommandPool());
        core::ImageUtilities::transitionImageLayout(transitionDepthImage.get(), offscreenResources.depthImage.depthImage,
                                                    vk::ImageLayout::eUndefined,
                                                    vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                                    vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);
        core::Utilities::endSingleTimeCommands(device, transitionDepthImage);
    }

    void VFXPreviewController::cleanupOffscreenResources()
    {
        for (auto const& resources : offscreenResources.colorImages)
        {
            device.getLogicalDevice().destroyImageView(resources.colorImageView);
            device.getLogicalDevice().destroyImage(resources.colorImage);
            device.getMemoryManager().free(resources.colorImageAllocation);
        }
        offscreenResources.colorImages.clear();

        if (offscreenResources.depthImage.depthImageView)
        {
            device.getLogicalDevice().destroyImageView(offscreenResources.depthImage.depthImageView);
            offscreenResources.depthImage.depthImageView = nullptr;
        }
        if (offscreenResources.depthImage.depthImage)
        {
            device.getLogicalDevice().destroyImage(offscreenResources.depthImage.depthImage);
            offscreenResources.depthImage.depthImage = nullptr;
        }
        if (offscreenResources.depthImage.depthImageAllocation)
        {
            device.getMemoryManager().free(offscreenResources.depthImage.depthImageAllocation);
            offscreenResources.depthImage.depthImageAllocation = {};
        }
    }

    void VFXPreviewController::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void VFXPreviewController::updateDescriptorSets(vk::DescriptorSet& descriptorSet,
                                                     const vk::ImageView& imageView) const
    {
        descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView,
                                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}
