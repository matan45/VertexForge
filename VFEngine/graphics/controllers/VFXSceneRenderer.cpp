#include "VFXSceneRenderer.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../render/vfx/scene/VFXScenePipeline.hpp"
#include "../render/vfx/particle/VFXParticleSystem.hpp"
#include "../render/vfx/compute/GPUVFXBufferManager.hpp"
#include "../render/vfx/compute/GPUVFXComputePipeline.hpp"
#include "../render/vfx/scene/VFXSceneGPUPipeline.hpp"
#include "../render/vfx/mesh/VFXMeshGPUPipeline.hpp"
#include "../render/vfx/ribbon/VFXRibbonGPUPipeline.hpp"
#include "../render/vfx/distortion/VFXDistortionPipeline.hpp"
#include "../render/vfx/particle/VFXEmitterPool.hpp"
#include "../render/mesh/MeshGPUCache.hpp"
#include "vfx/VFXEmitterConfigLoader.hpp"
#include "vfx/VFXModifierConfigLoader.hpp"
#include "print/Log.hpp"

namespace controllers
{
    VFXSceneRenderer::VFXSceneRenderer(core::Device& device, core::SwapChain& swapChain)
        : device{device}
        , swapChain{swapChain}
    {
    }

    VFXSceneRenderer::~VFXSceneRenderer()
    {
        cleanUp();
    }

    void VFXSceneRenderer::init(vk::RenderPass sceneRenderPass)
    {
        if (initialized)
        {
            return;
        }

        cpuPipeline = std::make_unique<render::vfx::VFXScenePipeline>(device, swapChain);
        cpuPipeline->init(sceneRenderPass);

        if (gpuDrivenEnabled)
        {
            if (!initGPUMode(sceneRenderPass))
            {
                vfLogWarning("GPU-driven VFX initialization failed, falling back to CPU mode");
                gpuDrivenEnabled = false;
            }
        }

        initialized = true;
        vfLogInfo("VFX Scene Renderer initialized (GPU mode: {})", gpuDrivenEnabled ? "enabled" : "disabled");
    }

    void VFXSceneRenderer::recreate(vk::RenderPass sceneRenderPass)
    {
        if (!initialized)
        {
            return;
        }

        if (cpuPipeline)
        {
            cpuPipeline->recreate(sceneRenderPass);
        }

        if (gpuRenderPipeline)
        {
            gpuRenderPipeline->recreate(sceneRenderPass);
        }

        if (gpuMeshPipeline)
        {
            gpuMeshPipeline->recreate(sceneRenderPass);
        }

        if (gpuRibbonPipeline)
        {
            gpuRibbonPipeline->recreate(sceneRenderPass);
        }
    }

    void VFXSceneRenderer::cleanUp()
    {
        if (!initialized)
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        destroyAllInstances();

        cleanupGPUMode();

        if (cpuPipeline)
        {
            cpuPipeline->cleanUp();
            cpuPipeline.reset();
        }

        initialized = false;
    }

    void VFXSceneRenderer::setGPUDrivenEnabled(bool enabled)
    {
        if (enabled == gpuDrivenEnabled)
        {
            return;
        }

        if (enabled && !gpuBufferManager)
        {
            vfLogWarning("Cannot enable GPU mode - GPU resources not initialized");
            return;
        }

        gpuDrivenEnabled = enabled;
        vfLogInfo("VFX GPU mode: {}", enabled ? "enabled" : "disabled");
    }

    VFXInstanceId VFXSceneRenderer::createInstance(const VFXRuntimeParams& params)
    {
        VFXInstanceId id = nextInstanceId++;

        VFXRuntimeInstance instance;
        instance.id = id;
        instance.worldTransform = params.worldTransform;
        instance.loop = params.loop;
        instance.entityId = params.entityId;
        instance.priority = params.priority;
        instance.cameraRelative = params.cameraRelative;

        if (!params.vfxAssetPath.empty())
        {
            auto configOpt = vfx::VFXEmitterConfigLoader::loadFromFile(params.vfxAssetPath);
            if (configOpt.has_value())
            {
                instance.config = configOpt.value();
            }
            else
            {
                vfLogWarning("Failed to load VFX asset: {}, using default config", params.vfxAssetPath);
            }
        }

        if (gpuDrivenEnabled && gpuBufferManager)
        {
            bool allocated = false;

            // Try pool first for fast allocation
            if (emitterPool)
            {
                auto poolResult = emitterPool->acquire();
                if (poolResult.valid())
                {
                    instance.gpuDriven = true;
                    instance.gpuEmitterIndex = poolResult.emitterIndex;
                    instance.gpuParticleOffset = poolResult.particleOffset;
                    instance.gpuParticleCount = poolResult.particleCount;
                    instance.active = false;
                    allocated = true;
                }
            }

            // Fallback to direct allocation
            if (!allocated)
            {
                auto allocation = gpuBufferManager->allocateEmitter(
                    render::vfx::GPUVFXConstants::DEFAULT_PARTICLES_PER_EMITTER);

                if (allocation.emitterIndex == UINT32_MAX)
                {
                    // Try priority eviction: find lowest-priority emitter below this one's priority
                    VFXInstanceId evictId = findLowestPriorityInstance(params.priority);
                    if (evictId != 0)
                    {
                        vfLogInfo("Evicting lower-priority VFX instance {} to make room for new instance {}", evictId, id);
                        destroyInstance(evictId);
                        allocation = gpuBufferManager->allocateEmitter(
                            render::vfx::GPUVFXConstants::DEFAULT_PARTICLES_PER_EMITTER);
                    }
                }

                if (allocation.emitterIndex != UINT32_MAX)
                {
                    instance.gpuDriven = true;
                    instance.gpuEmitterIndex = allocation.emitterIndex;
                    instance.gpuParticleOffset = allocation.particleOffset;
                    instance.gpuParticleCount = allocation.particleCount;
                    instance.active = false;
                }
                else
                {
                    vfLogWarning("GPU allocation failed for VFX instance {}, using CPU fallback", id);
                }
            }

            if (instance.gpuDriven)
            {
                vfLogDebug("Created GPU-driven VFX instance {} with {} particles at offset {}",
                           id, instance.gpuParticleCount, instance.gpuParticleOffset);
            }
        }

        if (!instance.gpuDriven)
        {
            instance.particleSystem = std::make_unique<render::vfx::VFXParticleSystem>();
            instance.particleSystem->setEmitterConfig(instance.config);
            instance.active = false;
        }

        instances[id] = std::move(instance);
        if (instances[id].gpuDriven)
        {
            emitterIndexToInstanceId[instances[id].gpuEmitterIndex] = id;
        }
        const auto& storedConfig = instances[id].config;
        if (!storedConfig.texturePath.empty() && cpuPipeline)
        {
            cpuPipeline->setTexture(storedConfig.texturePath);
        }

        glm::vec3 glowColor = ::vfx::VFXModifierConfigLoader::getGlowColorFromChain(storedConfig.modifiers);

        if (cpuPipeline)
        {
            render::vfx::VFXFlipbookConfig fbConfig;
            fbConfig.rows = storedConfig.flipbookRows;
            fbConfig.columns = storedConfig.flipbookColumns;
            fbConfig.alphaClipThreshold = storedConfig.alphaClipThreshold;
            fbConfig.additiveBlend = storedConfig.additiveBlend;
            fbConfig.renderMode = storedConfig.renderMode;
            fbConfig.stretchMultiplier = storedConfig.stretchMultiplier;
            fbConfig.glowColor = glowColor;
            cpuPipeline->setFlipbookConfig(fbConfig);
        }

        if (gpuRenderPipeline && instances[id].gpuDriven)
        {
            gpuRenderPipeline->setEmitterTexture(instances[id].gpuEmitterIndex, storedConfig.texturePath);
            gpuRenderPipeline->setEmitterRenderingConfig(instances[id].gpuEmitterIndex,
                                                          storedConfig.alphaClipThreshold,
                                                          storedConfig.additiveBlend,
                                                          glowColor);
            gpuRenderPipeline->setEmitterRenderMode(instances[id].gpuEmitterIndex,
                                                      static_cast<uint32_t>(storedConfig.renderMode));
            gpuRenderPipeline->setEmitterDistortionEnabled(instances[id].gpuEmitterIndex,
                                                            storedConfig.distortionEnabled);
        }
        
        if (gpuMeshPipeline && instances[id].gpuDriven &&
            storedConfig.renderMode == render::vfx::VFXRenderMode::MeshParticle)
        {
            gpuMeshPipeline->setEmitterMesh(instances[id].gpuEmitterIndex, storedConfig.meshPath);
            gpuMeshPipeline->setEmitterTexture(instances[id].gpuEmitterIndex, storedConfig.texturePath);
            gpuMeshPipeline->setEmitterRenderingConfig(instances[id].gpuEmitterIndex,
                                                        storedConfig.alphaClipThreshold,
                                                        storedConfig.additiveBlend,
                                                        glowColor);
        }

        if (gpuRibbonPipeline && instances[id].gpuDriven &&
            storedConfig.renderMode == render::vfx::VFXRenderMode::Ribbon)
        {
            gpuRibbonPipeline->setEmitterTexture(instances[id].gpuEmitterIndex, storedConfig.texturePath);
            gpuRibbonPipeline->setEmitterRenderingConfig(instances[id].gpuEmitterIndex,
                                                          storedConfig.alphaClipThreshold,
                                                          storedConfig.additiveBlend,
                                                          glowColor);
        }

        if (storedConfig.distortionEnabled)
        {
            activeDistortionCount++;
            if (gpuDistortionPipeline && instances[id].gpuDriven)
            {
                gpuDistortionPipeline->setEmitterDistortionTexture(
                    instances[id].gpuEmitterIndex, storedConfig.distortionTexturePath);
                gpuDistortionPipeline->setEmitterDistortionConfig(
                    instances[id].gpuEmitterIndex, storedConfig.distortionStrength);
            }
        }

        vfLogDebug("Created VFX instance {} from asset: {} (GPU: {})",
                   id, params.vfxAssetPath, instances[id].gpuDriven);
        return id;
    }

    void VFXSceneRenderer::destroyInstance(VFXInstanceId id)
    {
        auto it = instances.find(id);
        if (it != instances.end())
        {
            if (it->second.config.distortionEnabled && activeDistortionCount > 0)
                activeDistortionCount--;

            if (it->second.gpuDriven)
            {
                uint32_t emitterIdx = it->second.gpuEmitterIndex;
                emitterIndexToInstanceId.erase(emitterIdx);

                if (emitterPool)
                {
                    pendingEmitterFrees.emplace_back(emitterIdx, frameNumber);
                }
                else if (gpuBufferManager)
                {
                    pendingEmitterFrees.emplace_back(emitterIdx, frameNumber);
                }

                if (gpuMeshPipeline)
                    gpuMeshPipeline->removeEmitter(emitterIdx);
                if (gpuRibbonPipeline)
                    gpuRibbonPipeline->removeEmitter(emitterIdx);
            }

            instances.erase(it);

            std::vector<VFXInstanceId> subToDestroy;
            for (auto& sub : activeSubEmitters)
            {
                if (sub.parentId == id && !sub.finished)
                {
                    sub.finished = true;
                    subToDestroy.push_back(sub.subId);
                }
            }
            activeSubEmitters.erase(
                std::remove_if(activeSubEmitters.begin(), activeSubEmitters.end(),
                    [](const SubEmitterInstance& s) { return s.finished; }),
                activeSubEmitters.end());
            for (auto subId : subToDestroy)
            {
                destroyInstance(subId);
            }

            // When all instances are gone, fully reset GPU state for next Play cycle
            if (instances.empty())
            {
                activeSubEmitters.clear();
                pendingEmitterFrees.clear();
                if (emitterPool)
                {
                    emitterPool->reset();
                }
                if (gpuBufferManager)
                {
                    gpuBufferManager->resetParticleBufferClearedFlag();
                    gpuBufferManager->resetAllocator();
                }
                if (emitterPool && gpuBufferManager)
                {
                    emitterPool->warmUp();
                }
            }
        }
    }

    void VFXSceneRenderer::destroyAllInstances()
    {
        device.getLogicalDevice().waitIdle();

        activeSubEmitters.clear();
        pendingEmitterFrees.clear();
        activeDistortionCount = 0;
        instances.clear();
        emitterIndexToInstanceId.clear();

        if (emitterPool)
        {
            emitterPool->reset();
        }

        if (gpuBufferManager)
        {
            gpuBufferManager->resetParticleBufferClearedFlag();
            gpuBufferManager->resetAllocator();
        }

        if (emitterPool && gpuBufferManager)
        {
            emitterPool->warmUp();
        }
    }

    void VFXSceneRenderer::applyInstanceOverrides(VFXInstanceId id, const VFXEmitterOverrides& overrides)
    {
        auto it = instances.find(id);
        if (it == instances.end())
            return;

        auto& config = it->second.config;

        if (overrides.spawnRate.has_value())
            config.spawnRate = overrides.spawnRate.value();
        if (overrides.lifetime.has_value())
            config.lifetime = overrides.lifetime.value();
        if (overrides.startSize.has_value())
            config.startSize = overrides.startSize.value();
        if (overrides.startSpeed.has_value())
            config.startSpeed = overrides.startSpeed.value();
        if (overrides.stretchMultiplier.has_value())
            config.stretchMultiplier = overrides.stretchMultiplier.value();
        if (overrides.emitDirection.has_value())
            config.emitDirection = overrides.emitDirection.value();
        if (overrides.startColor.has_value())
            config.startColor = overrides.startColor.value();
        if (overrides.renderMode.has_value())
            config.renderMode = static_cast<render::vfx::VFXRenderMode>(overrides.renderMode.value());
        if (overrides.softParticleDistance.has_value())
            config.softParticleDistance = overrides.softParticleDistance.value();
        if (overrides.lightingInfluence.has_value())
            config.lightingInfluence = overrides.lightingInfluence.value();
        if (overrides.collisionEnabled.has_value())
            config.collisionEnabled = overrides.collisionEnabled.value();
        if (overrides.collisionLifetimeLoss.has_value())
            config.collisionLifetimeLoss = overrides.collisionLifetimeLoss.value();

        if (overrides.windDirection.has_value() || overrides.windStrength.has_value())
        {
            bool foundWind = false;
            for (auto& force : config.forces.forces)
            {
                if (auto* wind = std::get_if<::vfx::WindForceConfig>(&force))
                {
                    if (overrides.windDirection.has_value())
                        wind->direction = overrides.windDirection.value();
                    if (overrides.windStrength.has_value())
                        wind->strength = overrides.windStrength.value();
                    foundWind = true;
                    break;
                }
            }
            if (!foundWind)
            {
                ::vfx::WindForceConfig wind;
                if (overrides.windDirection.has_value())
                    wind.direction = overrides.windDirection.value();
                if (overrides.windStrength.has_value())
                    wind.strength = overrides.windStrength.value();
                config.forces.forces.push_back(wind);
            }
        }

        if (overrides.gravityStrength.has_value() || overrides.gravityDirection.has_value())
        {
            bool foundGravity = false;
            for (auto& force : config.forces.forces)
            {
                if (auto* gravity = std::get_if<::vfx::GravityForceConfig>(&force))
                {
                    if (overrides.gravityStrength.has_value())
                        gravity->strength = overrides.gravityStrength.value();
                    if (overrides.gravityDirection.has_value())
                        gravity->direction = overrides.gravityDirection.value();
                    foundGravity = true;
                    break;
                }
            }
            if (!foundGravity)
            {
                ::vfx::GravityForceConfig gravity;
                if (overrides.gravityDirection.has_value())
                    gravity.direction = overrides.gravityDirection.value();
                if (overrides.gravityStrength.has_value())
                    gravity.strength = overrides.gravityStrength.value();
                config.forces.forces.push_back(gravity);
            }
        }

        if (overrides.shapeDimensions.has_value())
        {
            config.shape.type = ::vfx::ShapeType::Box;
            config.shape.dimensions = glm::vec4(overrides.shapeDimensions.value(), 0.0f);
        }

        if (overrides.coneSpread.has_value())
            config.coneSpread = overrides.coneSpread.value();
    }

    void VFXSceneRenderer::setInstanceTransform(VFXInstanceId id, const glm::mat4& worldTransform)
    {
        auto it = instances.find(id);
        if (it != instances.end())
        {
            it->second.worldTransform = worldTransform;
        }
    }

    void VFXSceneRenderer::playInstance(VFXInstanceId id)
    {
        auto it = instances.find(id);
        if (it != instances.end())
        {
            if (it->second.gpuDriven)
            {
                it->second.firstFrame = true;
                it->second.active = true;
            }
            else if (it->second.particleSystem)
            {
                it->second.particleSystem->setPlaying(true);
                it->second.active = true;
            }
        }
    }

    void VFXSceneRenderer::stopInstance(VFXInstanceId id)
    {
        auto it = instances.find(id);
        if (it != instances.end())
        {
            if (it->second.gpuDriven)
            {
                it->second.active = false;
            }
            else if (it->second.particleSystem)
            {
                it->second.particleSystem->setPlaying(false);
            }
        }
    }

    void VFXSceneRenderer::resetInstance(VFXInstanceId id)
    {
        auto it = instances.find(id);
        if (it != instances.end())
        {
            if (it->second.gpuDriven)
            {
                it->second.spawnAccumulator = 0.0f;
                it->second.emissionTime = 0.0f;
                it->second.firstFrame = true;
                it->second.active = true;
            }
            else if (it->second.particleSystem)
            {
                it->second.particleSystem->reset();
                it->second.active = true;
            }
        }
    }

    bool VFXSceneRenderer::isInstancePlaying(VFXInstanceId id) const
    {
        auto it = instances.find(id);
        if (it != instances.end())
        {
            if (it->second.gpuDriven)
            {
                return it->second.active;
            }
            else if (it->second.particleSystem)
            {
                return it->second.particleSystem->isPlaying();
            }
        }
        return false;
    }

    bool VFXSceneRenderer::isInstanceActive(VFXInstanceId id) const
    {
        auto it = instances.find(id);
        if (it != instances.end())
        {
            return it->second.active;
        }
        return false;
    }

    std::optional<VFXSceneRenderer::PlaybackState> VFXSceneRenderer::capturePlaybackState(VFXInstanceId id) const
    {
        auto it = instances.find(id);
        if (it == instances.end())
            return std::nullopt;

        const auto& inst = it->second;
        PlaybackState state;
        state.emissionTime = inst.emissionTime;
        state.spawnAccumulator = inst.spawnAccumulator;
        state.wasActive = inst.active;
        state.wasPlaying = inst.active && (inst.loop || inst.emissionTime < inst.config.lifetime);
        return state;
    }

    void VFXSceneRenderer::seekInstance(VFXInstanceId id, float emissionTime, float spawnAccumulator)
    {
        auto it = instances.find(id);
        if (it == instances.end())
            return;

        auto& inst = it->second;
        inst.emissionTime = emissionTime;
        inst.spawnAccumulator = spawnAccumulator;
        inst.firstFrame = false;
    }

    void VFXSceneRenderer::update(float deltaTime)
    {
        processPendingEmitterFrees();

        if (gpuDrivenEnabled)
        {
            updateGPU(deltaTime);
        }
        else
        {
            updateCPU(deltaTime);
        }

        frameNumber++;

        if (gpuBufferManager)
        {
            gpuBufferManager->advanceFrame();
        }
    }

    void VFXSceneRenderer::processPendingEmitterFrees()
    {
        if (!gpuBufferManager || pendingEmitterFrees.empty())
        {
            return;
        }

        auto it = pendingEmitterFrees.begin();
        while (it != pendingEmitterFrees.end())
        {
            uint32_t emitterIndex = it->first;
            uint32_t destroyedFrame = it->second;

            uint32_t framesPassed = frameNumber - destroyedFrame;
            if (framesPassed >= FRAMES_BEFORE_FREE)
            {
                if (emitterPool)
                {
                    emitterPool->release(emitterIndex);
                }
                else
                {
                    gpuBufferManager->freeEmitter(emitterIndex);
                }
                it = pendingEmitterFrees.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void VFXSceneRenderer::updateCPU(float deltaTime)
    {
        for (auto& [id, instance] : instances)
        {
            if (instance.gpuDriven)
            {
                continue;
            }

            if (instance.particleSystem && instance.active)
            {
                instance.particleSystem->update(deltaTime);

                if (!instance.loop)
                {
                    size_t activeCount = instance.particleSystem->getActiveParticleCount();
                    if (activeCount == 0 && !instance.particleSystem->isPlaying())
                    {
                        instance.active = false;
                    }
                }
            }
        }
    }

    void VFXSceneRenderer::setCamera(const services::VFXCameraParams& camera)
    {
        currentView = camera.view;
        currentProjection = camera.projection;
        currentCameraPos = camera.cameraPos;
        currentTime = camera.time;

        extractFrustumPlanes(currentProjection * currentView);

        if (cpuPipeline && cpuPipeline->isInitialized())
        {
            cpuPipeline->updateCameraUBO(camera.view, camera.projection, camera.cameraPos, camera.time);
        }

        if (gpuRenderPipeline && gpuRenderPipeline->isInitialized())
        {
            gpuRenderPipeline->updateCameraUBO(camera.view, camera.projection, camera.cameraPos,
                                               camera.time, camera.nearPlane, camera.farPlane);
        }

        if (gpuMeshPipeline && gpuMeshPipeline->isInitialized())
        {
            gpuMeshPipeline->updateCameraUBO(camera.view, camera.projection, camera.cameraPos,
                                             camera.time, camera.nearPlane, camera.farPlane);
        }

        if (gpuRibbonPipeline && gpuRibbonPipeline->isInitialized())
        {
            gpuRibbonPipeline->updateCameraUBO(camera.view, camera.projection, camera.cameraPos,
                                               camera.time, camera.nearPlane, camera.farPlane);
        }
    }

    void VFXSceneRenderer::setSceneDepthImageView(vk::ImageView depthView)
    {
        if (gpuRenderPipeline && gpuRenderPipeline->isInitialized())
        {
            gpuRenderPipeline->setSceneDepthImageView(depthView);
        }

        if (gpuMeshPipeline && gpuMeshPipeline->isInitialized())
        {
            gpuMeshPipeline->setSceneDepthImageView(depthView);
        }

        if (gpuRibbonPipeline && gpuRibbonPipeline->isInitialized())
        {
            gpuRibbonPipeline->setSceneDepthImageView(depthView);
        }

        if (gpuDistortionPipeline && gpuDistortionPipeline->isInitialized())
        {
            gpuDistortionPipeline->setSceneDepthImageView(depthView);
        }
    }

    void VFXSceneRenderer::recordDrawCommands(vk::CommandBuffer cmd)
    {
        if (!initialized)
        {
            return;
        }

        if (gpuDrivenEnabled)
        {
            recordGPUDrawCommands(cmd);
        }

        recordCPUDrawCommands(cmd);
    }

    bool VFXSceneRenderer::hasDistortionEmitters() const
    {
        return activeDistortionCount > 0;
    }

    void VFXSceneRenderer::recordDistortionDrawCommands(vk::CommandBuffer cmd)
    {
        if (!gpuDistortionPipeline || !gpuDistortionPipeline->isInitialized() || !gpuBufferManager)
            return;

        uint32_t maxEmitters = gpuBufferManager->getMaxEmitters();
        std::vector<bool> distortionFlags(maxEmitters, false);
        bool anyEnabled = false;

        for (const auto& [id, instance] : instances)
        {
            if (instance.gpuDriven && instance.active && instance.config.distortionEnabled
                && instance.gpuEmitterIndex < maxEmitters)
            {
                distortionFlags[instance.gpuEmitterIndex] = true;
                anyEnabled = true;
            }
        }

        if (!anyEnabled)
            return;

        gpuDistortionPipeline->updateCameraUBO(currentView, currentProjection,
            currentCameraPos, currentTime);

        gpuDistortionPipeline->recordCommandsInline(
            cmd,
            gpuBufferManager->getDrawCommandBuffer(),
            maxEmitters,
            distortionFlags);
    }

    void VFXSceneRenderer::recordCPUDrawCommands(vk::CommandBuffer cmd)
    {
        if (!cpuPipeline || !cpuPipeline->isInitialized())
        {
            return;
        }

        collectAllParticleInstances();

        if (collectedInstances.empty())
        {
            return;
        }

        cpuPipeline->setParticleInstances(collectedInstances);
        cpuPipeline->recordCommandsInline(cmd);
    }

    void VFXSceneRenderer::collectAllParticleInstances()
    {
        collectedInstances.clear();

        for (const auto& [id, instance] : instances)
        {
            if (instance.gpuDriven)
            {
                continue;
            }

            if (!instance.particleSystem || !instance.active)
            {
                continue;
            }

            auto particleData = instance.particleSystem->getInstanceData();

            for (auto& particle : particleData)
            {
                glm::vec4 worldPos = instance.worldTransform * glm::vec4(particle.worldPosition, 1.0f);
                particle.worldPosition = glm::vec3(worldPos);
            }

            collectedInstances.insert(collectedInstances.end(), particleData.begin(), particleData.end());
        }
    }

    VFXSceneRenderer::VFXBudgetStats VFXSceneRenderer::getBudgetStats() const
    {
        VFXBudgetStats stats{};

        if (gpuBufferManager)
        {
            stats.activeEmitters = gpuBufferManager->getActiveEmitterCount();
            stats.maxEmitters = gpuBufferManager->getMaxEmitters();
            stats.allocatedParticles = gpuBufferManager->getAllocatedParticleCount();
            stats.maxParticles = gpuBufferManager->getMaxParticles();
            stats.fragmentationPercent = gpuBufferManager->getFragmentationPercent();
        }

        for (const auto& [id, instance] : instances)
        {
            if (instance.gpuDriven && instance.active && instance.currentLOD < 4)
            {
                stats.lodCounts[instance.currentLOD]++;
            }
        }

        if (emitterPool)
        {
            stats.poolWarmSlots = emitterPool->getWarmSlotCount();
            stats.poolUsedSlots = emitterPool->getUsedSlotCount();
            stats.poolTotalSlots = emitterPool->getTotalSlotCount();
        }

        return stats;
    }

    VFXSceneRenderer::VFXLODConfig VFXSceneRenderer::getLODConfig() const
    {
        return {LOD0_DIST, LOD1_DIST, LOD2_DIST, LOD_TRANSITION_ZONE};
    }

    void VFXSceneRenderer::setLODConfig(const VFXLODConfig& config)
    {
        LOD0_DIST = config.lod0Distance;
        LOD1_DIST = config.lod1Distance;
        LOD2_DIST = config.lod2Distance;
        LOD_TRANSITION_ZONE = config.transitionZone;
    }

    size_t VFXSceneRenderer::getTotalParticleCount() const
    {
        size_t total = 0;
        for (const auto& [id, instance] : instances)
        {
            if (!instance.active)
            {
                continue;
            }

            if (instance.gpuDriven)
            {
                total += instance.gpuParticleCount;
            }
            else if (instance.particleSystem)
            {
                total += instance.particleSystem->getActiveParticleCount();
            }
        }
        return total;
    }
}
