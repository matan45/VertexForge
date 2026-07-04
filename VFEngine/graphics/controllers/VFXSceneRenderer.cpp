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
#include "vfx/VFXSortOrder.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cmath>
#include <random>

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

    void VFXSceneRenderer::init(vk::Format colorFormat, vk::Format depthFormat)
    {
        if (initialized)
        {
            return;
        }

        cpuPipeline = std::make_unique<render::vfx::VFXScenePipeline>(device, swapChain);
        cpuPipeline->init(colorFormat, depthFormat);

        if (gpuDrivenEnabled)
        {
            if (!initGPUMode(colorFormat, depthFormat))
            {
                vfLogWarning("GPU-driven VFX initialization failed, falling back to CPU mode");
                gpuDrivenEnabled = false;
            }
        }

        initialized = true;
        vfLogInfo("VFX Scene Renderer initialized (GPU mode: {})", gpuDrivenEnabled ? "enabled" : "disabled");
    }

    void VFXSceneRenderer::recreate(vk::Format colorFormat, vk::Format depthFormat)
    {
        if (!initialized)
        {
            return;
        }

        if (cpuPipeline)
        {
            cpuPipeline->recreate(colorFormat, depthFormat);
        }

        if (gpuRenderPipeline)
        {
            gpuRenderPipeline->recreate(colorFormat, depthFormat);
        }

        if (gpuMeshPipeline)
        {
            gpuMeshPipeline->recreate(colorFormat, depthFormat);
        }

        if (gpuRibbonPipeline)
        {
            gpuRibbonPipeline->recreate(colorFormat, depthFormat);
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
        // Load the base emitter config (cache-aware: only successful loads are cached).
        render::vfx::VFXEmitterConfig baseConfig;
        if (!params.vfxAssetPath.empty())
        {
            baseConfig = loadConfigCached(params.vfxAssetPath);
        }

        // VK-1453: resolve the per-tier scalability level. A disabled profile yields a
        // neutral level, so effects without a profile behave byte-identically.
        const vfx::VFXScalabilityLevel level = vfx::resolveScalability(baseConfig.scalability, currentTier);
        if (!level.rendererEnabled)
        {
            return 0; // this effect is not spawned/drawn at the active quality tier
        }

        VFXInstanceId id = 0;
        bool revived = false;

        // VK-1453: reuse a dormant fire-and-forget instance of the same asset when
        // pooling is requested, instead of allocating a fresh id/record.
        if (params.poolable && !params.vfxAssetPath.empty())
        {
            VFXInstanceId reviveId = instancePool.acquire(params.vfxAssetPath);
            if (reviveId != 0)
            {
                auto it = instances.find(reviveId);
                if (it != instances.end())
                {
                    id = reviveId;
                    reviveDormantInstance(it->second, params, baseConfig, level);
                    revived = true;
                }
                // else: stale pool entry (record already gone) — fall through to fresh create.
            }
        }

        if (!revived)
        {
            id = nextInstanceId++;

            VFXRuntimeInstance instance;
            instance.id = id;
            instance.worldTransform = params.worldTransform;
            instance.loop = params.loop;
            instance.entityId = params.entityId;
            instance.priority = params.priority;
            instance.cameraRelative = params.cameraRelative;
            instance.autoDestroy = params.autoDestroy;
            instance.poolable = params.poolable;
            instance.assetPath = params.vfxAssetPath;

            // VK-1451: a stable per-instance RNG seed, chosen ONCE here. An explicit seed
            // (combo determinism) makes the emission schedule reproducible; otherwise pick
            // a random seed once so playback still varies between fresh effects — but no
            // longer re-randomizes every frame as it did before.
            instance.seed = pickInstanceSeed(params.seed);

            // Apply the resolved scalability level to the per-instance config/state.
            instance.config = baseConfig;
            instance.config.spawnRate *= level.spawnRateScale;
            if (level.cullDistance > 0.0f)
            {
                instance.cullDistanceSqOverride = level.cullDistance * level.cullDistance;
            }
            if (level.updateInterval > 1)
            {
                instance.updateInterval = level.updateInterval;
                instance.updatePhase = static_cast<int>(id % static_cast<uint32_t>(level.updateInterval));
            }

            // Acquire a GPU emitter slot (or fall back to CPU simulation).
            configureInstanceEmitter(id, instance, params.priority, level.maxParticles);

            instances[id] = std::move(instance);
        }

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
            fbConfig.blendMode = storedConfig.blendMode; // VK-1472
            fbConfig.renderMode = storedConfig.renderMode;
            fbConfig.stretchMultiplier = storedConfig.stretchMultiplier;
            fbConfig.glowColor = glowColor;
            fbConfig.emissiveIntensity = storedConfig.emissiveIntensity;
            fbConfig.frameBlend = storedConfig.flipbookFrameBlend;
            fbConfig.frameRate = storedConfig.flipbookFrameRate;
            cpuPipeline->setFlipbookConfig(fbConfig);
        }

        if (gpuRenderPipeline && instances[id].gpuDriven)
        {
            gpuRenderPipeline->setEmitterTexture(instances[id].gpuEmitterIndex, storedConfig.texturePath);
            gpuRenderPipeline->setEmitterRenderingConfig(instances[id].gpuEmitterIndex,
                                                          storedConfig.alphaClipThreshold,
                                                          storedConfig.blendMode,
                                                          glowColor,
                                                          storedConfig.sortOrder);
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
                                                        storedConfig.blendMode,
                                                        glowColor,
                                                        storedConfig.sortOrder);
        }

        if (gpuRibbonPipeline && instances[id].gpuDriven &&
            storedConfig.renderMode == render::vfx::VFXRenderMode::Ribbon)
        {
            gpuRibbonPipeline->setEmitterTexture(instances[id].gpuEmitterIndex, storedConfig.texturePath);
            gpuRibbonPipeline->setEmitterRenderingConfig(instances[id].gpuEmitterIndex,
                                                          storedConfig.alphaClipThreshold,
                                                          storedConfig.blendMode,
                                                          glowColor,
                                                          storedConfig.sortOrder);
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

    render::vfx::VFXEmitterConfig VFXSceneRenderer::loadConfigCached(const std::string& path)
    {
        auto cacheIt = configCache.find(path);
        if (cacheIt != configCache.end())
        {
            return cacheIt->second;
        }

        auto configOpt = vfx::VFXEmitterConfigLoader::loadFromFile(path);
        if (configOpt.has_value())
        {
            // Cache only successful loads so a missing file keeps retrying (and warning)
            // exactly as before, while repeat spawns of a valid asset skip the re-parse.
            auto inserted = configCache.emplace(path, std::move(configOpt.value()));
            return inserted.first->second;
        }

        vfLogWarning("Failed to load VFX asset: {}, using default config", path);
        return render::vfx::VFXEmitterConfig{};
    }

    void VFXSceneRenderer::invalidateConfigCache(const std::string& path)
    {
        configCache.erase(path);
    }

    uint32_t VFXSceneRenderer::pickInstanceSeed(uint32_t explicitSeed)
    {
        if (explicitSeed != 0)
        {
            return explicitSeed;
        }
        static std::random_device seedRd;
        static std::mt19937 seedGen(seedRd());
        uint32_t s = std::uniform_int_distribution<uint32_t>{}(seedGen);
        return s == 0 ? 1u : s; // never return the 0 sentinel
    }

    void VFXSceneRenderer::configureInstanceEmitter(VFXInstanceId id, VFXRuntimeInstance& instance,
                                                    services::VFXEmitterPriority priority, int maxParticlesCap)
    {
        // Reset allocation state so this works for both a freshly-built instance and a
        // revived dormant one (stale GPU/CPU fields cleared).
        instance.gpuDriven = false;
        instance.gpuEmitterIndex = UINT32_MAX;
        instance.gpuParticleOffset = 0;
        instance.gpuParticleCount = 0;
        instance.particleSystem.reset();

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
                    VFXInstanceId evictId = findLowestPriorityInstance(priority);
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
                // VK-1453: honor a per-tier particle cap on the simulated count.
                if (maxParticlesCap > 0)
                {
                    instance.gpuParticleCount =
                        std::min(instance.gpuParticleCount, static_cast<uint32_t>(maxParticlesCap));
                }

                vfLogDebug("Created GPU-driven VFX instance {} with {} particles at offset {}",
                           id, instance.gpuParticleCount, instance.gpuParticleOffset);
            }
        }

        if (!instance.gpuDriven)
        {
            instance.particleSystem = std::make_unique<render::vfx::VFXParticleSystem>();
            instance.particleSystem->setEmitterConfig(instance.config);
            // VK-1460: apply the stable per-instance seed on the CPU fallback too (the
            // GPU path already feeds instance.seed via toGPUConfig). Without this,
            // spawnComboSeeded / explicit seeds are non-deterministic for CPU-simulated
            // effects — setSeed was otherwise only called in the preview controller.
            instance.particleSystem->setSeed(instance.seed);
            instance.active = false;
        }
    }

    void VFXSceneRenderer::reviveDormantInstance(VFXRuntimeInstance& instance,
                                                 const VFXRuntimeParams& params,
                                                 const render::vfx::VFXEmitterConfig& baseConfig,
                                                 const vfx::VFXScalabilityLevel& level)
    {
        const VFXInstanceId id = instance.id;

        // Reset playback + transform state to a fresh spawn.
        instance.worldTransform = params.worldTransform;
        instance.prevWorldTransform = params.worldTransform;
        instance.emitterVelocity = glm::vec3(0.0f);
        instance.loop = params.loop;
        instance.entityId = params.entityId;
        instance.priority = params.priority;
        instance.cameraRelative = params.cameraRelative;
        instance.autoDestroy = params.autoDestroy;
        instance.poolable = params.poolable;
        instance.assetPath = params.vfxAssetPath;
        instance.seed = pickInstanceSeed(params.seed);

        instance.spawnAccumulator = 0.0f;
        instance.emissionTime = 0.0f;
        instance.firstFrame = true;
        instance.currentLOD = 0;
        instance.lodSpawnMultiplier = 1.0f;
        instance.lodBias = 0.0f;
        instance.burstClampWarned = false;
        instance.dormant = false;

        // Reset scalability-derived per-instance state, then re-apply for this spawn.
        instance.cullDistanceSqOverride = -1.0f;
        instance.updateInterval = 1;
        instance.updatePhase = 0;

        instance.config = baseConfig;
        instance.config.spawnRate *= level.spawnRateScale;
        if (level.cullDistance > 0.0f)
        {
            instance.cullDistanceSqOverride = level.cullDistance * level.cullDistance;
        }
        if (level.updateInterval > 1)
        {
            instance.updateInterval = level.updateInterval;
            instance.updatePhase = static_cast<int>(id % static_cast<uint32_t>(level.updateInterval));
        }

        configureInstanceEmitter(id, instance, params.priority, level.maxParticles);

        vfLogDebug("Revived dormant VFX instance {} for asset: {} (GPU: {})",
                   id, params.vfxAssetPath, instance.gpuDriven);
    }

    void VFXSceneRenderer::retireInstanceToDormant(VFXInstanceId id)
    {
        auto it = instances.find(id);
        if (it == instances.end())
        {
            return;
        }

        VFXRuntimeInstance& instance = it->second;

        // Distortion accounting mirrors destroyInstance; the count is re-incremented by
        // the pipeline-config path when the slot is revived, so it stays balanced.
        if (instance.config.distortionEnabled && activeDistortionCount > 0)
        {
            activeDistortionCount--;
        }

        // Release the GPU emitter slot through the same deferred-free path as a real
        // destroy, but keep the instance record around for later revival.
        if (instance.gpuDriven)
        {
            uint32_t emitterIdx = instance.gpuEmitterIndex;
            emitterIndexToInstanceId.erase(emitterIdx);

            if (emitterPool || gpuBufferManager)
            {
                pendingEmitterFrees.emplace_back(emitterIdx, frameNumber);
            }

            if (gpuMeshPipeline)
                gpuMeshPipeline->removeEmitter(emitterIdx);
            if (gpuRibbonPipeline)
                gpuRibbonPipeline->removeEmitter(emitterIdx);
        }

        // A retired parent takes its live sub-emitters with it (mirrors destroyInstance).
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

        // Mark dormant and clear the freed GPU/CPU allocation state.
        instance.dormant = true;
        instance.active = false;
        instance.gpuDriven = false;
        instance.gpuEmitterIndex = UINT32_MAX;
        instance.gpuParticleCount = 0;
        instance.particleSystem.reset();

        // Register with the dormant pool; if it is full, truly destroy the evicted oldest.
        VFXInstanceId evicted = instancePool.retain(id, instance.assetPath);
        if (evicted != 0)
        {
            destroyInstance(evicted);
        }
    }

    void VFXSceneRenderer::destroyInstance(VFXInstanceId id)
    {
        auto it = instances.find(id);
        if (it != instances.end())
        {
            // A dormant instance already released its distortion count when it was retired;
            // don't double-decrement when the pool later evicts and truly destroys it.
            if (!it->second.dormant && it->second.config.distortionEnabled && activeDistortionCount > 0)
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
        configCache.clear();
        instancePool.clear();

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
        // VK-1453: per-frame cull/throttle counters, reported via getBudgetStats.
        culledEmittersThisFrame = 0;
        throttledEmittersThisFrame = 0;

        processPendingEmitterFrees();

        if (gpuDrivenEnabled)
        {
            updateGPU(deltaTime);
        }
        else
        {
            updateCPU(deltaTime);
        }

        // Fire-and-forget instances: destroy once emission is done and the
        // longest-lived particles have expired (same margin as sub-emitters)
        std::vector<VFXInstanceId> finishedAutoDestroy;
        for (const auto& [id, instance] : instances)
        {
            if (instance.autoDestroy && !instance.loop && instance.active &&
                instance.emissionTime >= instance.config.lifetime * 2.0f)
            {
                finishedAutoDestroy.push_back(id);
            }
        }
        for (VFXInstanceId id : finishedAutoDestroy)
        {
            // VK-1453: poolable fire-and-forget effects are retired to the dormant pool
            // (cheap reuse by a later spawn of the same asset) instead of destroyed.
            auto it = instances.find(id);
            if (it != instances.end() && it->second.poolable && !it->second.assetPath.empty())
            {
                retireInstanceToDormant(id);
            }
            else
            {
                destroyInstance(id);
            }
        }

        frameNumber++;

        if (gpuBufferManager)
        {
            gpuBufferManager->advanceFrame();
        }
    }

    std::vector<VFXSceneRenderer::VFXProxyLight> VFXSceneRenderer::getActiveProxyLights() const
    {
        constexpr size_t MAX_PROXY_LIGHTS = 64;

        std::vector<VFXProxyLight> lights;
        for (const auto& [id, instance] : instances)
        {
            if (!instance.active || !instance.config.lightEmissionEnabled)
                continue;
            if (lights.size() >= MAX_PROXY_LIGHTS)
                break;

            VFXProxyLight light;
            light.position = glm::vec3(instance.worldTransform[3]);
            light.color = glm::vec3(instance.config.startColor);
            light.intensity = instance.config.lightEmissionIntensity;
            light.radius = instance.config.lightEmissionRadius;

            // One-shot effects decay their light over the instance lifetime
            if (!instance.loop && instance.config.lifetime > 0.0f)
            {
                float fade = 1.0f - std::clamp(
                    instance.emissionTime / (instance.config.lifetime * 2.0f), 0.0f, 1.0f);
                light.intensity *= fade;
                if (light.intensity <= 0.01f)
                    continue;
            }

            lights.push_back(light);
        }
        return lights;
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

        // VK-1471: flatten CPU-sim emitters in ascending sortOrder so a higher-sortOrder
        // emitter's particles land later in the merged instanced draw (drawn on top).
        // Gathered in the map's current traversal order, so an all-default (0) set is a
        // stable no-op and reproduces today's order.
        std::vector<::vfx::VFXDrawOrderEntry> drawOrder;
        std::vector<const VFXRuntimeInstance*> liveInstances;
        drawOrder.reserve(instances.size());
        liveInstances.reserve(instances.size());

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

            drawOrder.push_back({static_cast<uint32_t>(liveInstances.size()), instance.config.sortOrder});
            liveInstances.push_back(&instance);
        }

        ::vfx::stableSortDrawOrder(drawOrder);

        for (const auto& drawEntry : drawOrder)
        {
            const VFXRuntimeInstance& instance = *liveInstances[drawEntry.index];

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

        // VK-1453 (Phase 4)
        stats.culledEmitters = culledEmittersThisFrame;
        stats.throttledEmitters = throttledEmittersThisFrame;
        stats.vfxCullDistance = std::sqrt(maxVFXDistSq);

        return stats;
    }

    VFXSceneRenderer::VFXCullState VFXSceneRenderer::getCullState() const
    {
        VFXCullState state;
        state.valid = frustumPlanesValid;
        state.viewProj = currentProjection * currentView;
        state.cameraPos = currentCameraPos;
        state.distanceCullEnabled = distanceCullingEnabled;
        state.maxDrawDistance = std::sqrt(maxVFXDistSq);
        return state;
    }

    std::vector<VFXSceneRenderer::VFXInstanceDebugInfo> VFXSceneRenderer::getInstanceDebugInfo() const
    {
        constexpr size_t MAX_DEBUG_INSTANCES = 64;

        std::vector<VFXInstanceDebugInfo> out;
        out.reserve(std::min(instances.size(), MAX_DEBUG_INSTANCES));

        for (const auto& [id, instance] : instances)
        {
            if (instance.dormant)
                continue; // dormant (pooled) instances are inert — not shown
            if (out.size() >= MAX_DEBUG_INSTANCES)
                break;

            const auto& cfg = instance.config;

            // Symmetric extents from the same conservative radius the frustum cull uses.
            float maxDim = glm::max(glm::max(
                std::abs(cfg.shape.dimensions.x),
                std::abs(cfg.shape.dimensions.y)),
                std::abs(cfg.shape.dimensions.z));
            float radius = std::max(cfg.lifetime * cfg.startSpeed, maxDim) + 5.0f;

            VFXInstanceDebugInfo info;
            info.id = id;
            info.worldPosition = glm::vec3(instance.worldTransform[3]);
            info.extents = glm::vec3(radius);
            info.inFrustum = isEmitterInFrustum(instance);
            info.lod = instance.currentLOD;
            info.particleCount = instance.gpuParticleCount;
            info.priority = static_cast<uint8_t>(instance.priority);
            out.push_back(info);
        }
        return out;
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
