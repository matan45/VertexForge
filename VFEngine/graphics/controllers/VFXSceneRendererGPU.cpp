#include "VFXSceneRenderer.hpp"
#include "../render/vfx/GPUVFXBufferManager.hpp"
#include "../render/vfx/GPUVFXComputePipeline.hpp"
#include "../render/vfx/VFXSceneGPUPipeline.hpp"
#include "../render/vfx/VFXMeshGPUPipeline.hpp"
#include "../render/vfx/VFXRibbonGPUPipeline.hpp"
#include "../render/vfx/VFXEmitterPool.hpp"
#include "../render/vfx/VFXParticleSystem.hpp"
#include "../render/vfx/VFXLUTBaker.hpp"
#include "../render/mesh/MeshGPUCache.hpp"
#include "../core/RenderManager.hpp"
#include "vfx/VFXModifierTypes.hpp"
#include "vfx/VFXForceTypes.hpp"
#include "vfx/VFXShapeTypes.hpp"
#include "vfx/VFXEmitterConfigLoader.hpp"
#include "../../services/events/vfx/VFXEventNotifications.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "print/Log.hpp"
#include <random>
#include <type_traits>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace controllers
{
    bool VFXSceneRenderer::initGPUMode(vk::RenderPass renderPass)
    {
        try
        {
            gpuBufferManager = std::make_unique<render::vfx::GPUVFXBufferManager>(device);
            if (!gpuBufferManager->init())
            {
                vfLogError("Failed to initialize GPU VFX buffer manager");
                return false;
            }

            gpuComputePipeline = std::make_unique<render::vfx::GPUVFXComputePipeline>(device);
            gpuComputePipeline->init();
            if (!gpuComputePipeline->isInitialized())
            {
                vfLogError("Failed to initialize GPU VFX compute pipeline");
                return false;
            }

            gpuRenderPipeline = std::make_unique<render::vfx::VFXSceneGPUPipeline>(device, swapChain);
            gpuRenderPipeline->init(renderPass);
            if (!gpuRenderPipeline->isInitialized())
            {
                vfLogError("Failed to initialize GPU VFX render pipeline");
                return false;
            }
            gpuRenderPipeline->setDeletionQueue(core::RenderManager::getGlobalDeletionQueue());
            
            gpuMeshCache = std::make_unique<render::mesh::MeshGPUCache>(device);
            gpuMeshPipeline = std::make_unique<render::vfx::VFXMeshGPUPipeline>(device, swapChain, *gpuMeshCache);
            gpuMeshPipeline->init(renderPass);
            if (!gpuMeshPipeline->isInitialized())
            {
                vfLogError("Failed to initialize GPU VFX mesh pipeline");
                return false;
            }
            gpuMeshPipeline->setDeletionQueue(core::RenderManager::getGlobalDeletionQueue());

            gpuRibbonPipeline = std::make_unique<render::vfx::VFXRibbonGPUPipeline>(device, swapChain);
            gpuRibbonPipeline->init(renderPass);
            if (!gpuRibbonPipeline->isInitialized())
            {
                vfLogError("Failed to initialize GPU VFX ribbon pipeline");
                return false;
            }
            gpuRibbonPipeline->setDeletionQueue(core::RenderManager::getGlobalDeletionQueue());

            gpuComputePipeline->updateDescriptors(gpuBufferManager->getBufferSet());

            gpuRenderPipeline->updateParticleBuffer(
                gpuBufferManager->getParticleBuffer(),
                gpuBufferManager->getParticleBufferSize()
            );

            gpuRenderPipeline->updateConfigBuffer(
                gpuBufferManager->getConfigBuffer(),
                gpuBufferManager->getConfigBufferSize()
            );
            
            gpuMeshPipeline->updateParticleBuffer(
                gpuBufferManager->getParticleBuffer(),
                gpuBufferManager->getParticleBufferSize()
            );

            gpuMeshPipeline->updateConfigBuffer(
                gpuBufferManager->getConfigBuffer(),
                gpuBufferManager->getConfigBufferSize()
            );

            gpuRibbonPipeline->updateParticleBuffer(
                gpuBufferManager->getParticleBuffer(),
                gpuBufferManager->getParticleBufferSize()
            );

            gpuRibbonPipeline->updateConfigBuffer(
                gpuBufferManager->getConfigBuffer(),
                gpuBufferManager->getConfigBufferSize()
            );

            gpuRibbonPipeline->updateRibbonBuffers(
                gpuBufferManager->getRibbonRingBuffer(),
                gpuBufferManager->getRibbonRingBufferSize(),
                gpuBufferManager->getRibbonHeadBuffer(),
                gpuBufferManager->getRibbonHeadBufferSize()
            );

            // Create emitter pool with warm slots for fast streaming reuse
            emitterPool = std::make_unique<render::vfx::VFXEmitterPool>(*gpuBufferManager, 32);
            emitterPool->warmUp();

            vfLogInfo("GPU VFX mode initialized: {} max particles, {} max emitters",
                       gpuBufferManager->getMaxParticles(),
                       gpuBufferManager->getMaxEmitters());
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Exception during GPU VFX initialization: {}", e.what());
            cleanupGPUMode();
            return false;
        }
    }

    void VFXSceneRenderer::cleanupGPUMode()
    {
        if (emitterPool)
        {
            emitterPool->reset();
            emitterPool.reset();
        }

        if (gpuRibbonPipeline)
        {
            gpuRibbonPipeline->cleanup();
            gpuRibbonPipeline.reset();
        }

        if (gpuMeshPipeline)
        {
            gpuMeshPipeline->cleanup();
            gpuMeshPipeline.reset();
        }

        if (gpuMeshCache)
        {
            gpuMeshCache->unloadAllMeshes();
            gpuMeshCache.reset();
        }

        if (gpuRenderPipeline)
        {
            gpuRenderPipeline->cleanup();
            gpuRenderPipeline.reset();
        }

        if (gpuComputePipeline)
        {
            gpuComputePipeline->cleanup();
            gpuComputePipeline.reset();
        }

        if (gpuBufferManager)
        {
            gpuBufferManager->cleanup();
            gpuBufferManager.reset();
        }
    }

    void VFXSceneRenderer::setSceneColliders(const std::vector<render::vfx::GPUCollider>& colliders)
    {
        sceneColliders = colliders;
        sceneColliderCount = static_cast<uint32_t>(
            std::min(colliders.size(), static_cast<size_t>(render::vfx::GPUVFXConstants::MAX_SCENE_COLLIDERS)));
    }

    void VFXSceneRenderer::setTerrainHeightfield(const render::vfx::GPUTerrainHeightfield& header,
                                                  std::vector<float> heights)
    {
        terrainHeader = header;
        terrainHeights = std::move(heights);
        terrainDirty = true;
    }

    void VFXSceneRenderer::updateGPU(float deltaTime)
    {
        if (!gpuBufferManager)
        {
            return;
        }

        // Upload scene colliders to GPU
        if (sceneColliderCount > 0)
        {
            gpuBufferManager->updateSceneColliders(sceneColliders, sceneColliderCount);
        }

        // Upload terrain heightfield to GPU
        if (terrainDirty)
        {
            if (terrainHeader.enabled && !terrainHeights.empty())
            {
                gpuBufferManager->updateTerrainHeightfield(
                    terrainHeader, terrainHeights.data(),
                    static_cast<uint32_t>(terrainHeights.size()));
            }
            else
            {
                gpuBufferManager->clearTerrainHeightfield();
            }
            terrainDirty = false;
        }

        lastFrameEvents = gpuBufferManager->readbackEvents(lastFrameEventCount);
        processEvents();
        cleanupFinishedSubEmitters(deltaTime);

        for (auto& [id, instance] : instances)
        {
            if (!instance.gpuDriven && instance.particleSystem && instance.active)
            {
                instance.particleSystem->update(deltaTime);
            }
        }

        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dist;

        for (auto& [id, instance] : instances)
        {
            if (!instance.gpuDriven || !instance.active)
            {
                continue;
            }

            // Update LOD based on camera distance
            updateInstanceLOD(instance);

            // Override transform for camera-relative emitters (weather effects)
            if (instance.cameraRelative)
            {
                instance.worldTransform = glm::translate(glm::mat4(1.0f), currentCameraPos);
            }

            // Clamp deltaTime on first active frame to prevent particle burst
            float effectiveDt = deltaTime;
            if (instance.firstFrame)
            {
                effectiveDt = std::min(deltaTime, 1.0f / 30.0f);
                instance.firstFrame = false;
            }

            instance.emissionTime += effectiveDt;

            uint32_t spawnThisFrame = 0;
            bool canSpawn = instance.loop || (instance.emissionTime < instance.config.lifetime);

            if (instance.active && canSpawn)
            {
                // Apply LOD spawn rate multiplier
                float lodAdjustedRate = instance.config.spawnRate * instance.lodSpawnMultiplier;
                instance.spawnAccumulator += lodAdjustedRate * effectiveDt;
                spawnThisFrame = static_cast<uint32_t>(instance.spawnAccumulator);
                instance.spawnAccumulator -= static_cast<float>(spawnThisFrame);
            }

            auto gpuConfig = toGPUConfig(
                instance.config,
                effectiveDt,
                instance.gpuParticleCount,
                dist(gen)
            );

            // Set collider count based on collision enabled per emitter
            // LOD 1+: disable collision
            gpuConfig.colliderCount = (instance.config.collisionEnabled && instance.currentLOD == 0)
                                          ? sceneColliderCount : 0;

            // LOD 2+: disable soft particles
            if (instance.currentLOD >= 2)
            {
                gpuConfig.softParticleDistance = 0.0f;
            }

            // Apply LOD-scaled spawn rate to GPU config
            gpuConfig.spawnRate = instance.config.spawnRate * instance.lodSpawnMultiplier;

            if (instance.config.renderMode == render::vfx::VFXRenderMode::MeshParticle && gpuMeshPipeline)
            {
                gpuConfig.drawIndexCount = gpuMeshPipeline->getEmitterMeshIndexCount(instance.gpuEmitterIndex);
            }

            auto lutResult = render::vfx::VFXLUTBaker::bake(instance.config.modifiers);
            if (lutResult.lutFlags != 0)
            {
                gpuBufferManager->updateEmitterLUT(instance.gpuEmitterIndex, lutResult.data);
                gpuConfig.lutBaseOffset = instance.gpuEmitterIndex *
                    render::vfx::GPUVFXConstants::LUT_CHANNELS *
                    render::vfx::GPUVFXConstants::LUT_RESOLUTION;
                gpuConfig.lutChannelStride = render::vfx::GPUVFXConstants::LUT_RESOLUTION;
                gpuConfig.lutFlags = lutResult.lutFlags;
            }

            gpuBufferManager->updateEmitterConfig(instance.gpuEmitterIndex, gpuConfig);

            auto gpuState = toGPUState(instance);
            gpuState.spawnThisFrame = spawnThisFrame;
            gpuBufferManager->updateEmitterState(instance.gpuEmitterIndex, gpuState);
        }
    }

    render::vfx::GPUEmitterConfig VFXSceneRenderer::toGPUConfig(
        const render::vfx::VFXEmitterConfig& cpuConfig,
        float deltaTime,
        uint32_t maxParticles,
        uint32_t seed) const
    {
        render::vfx::GPUEmitterConfig gpuConfig{};
        gpuConfig.emitDirection = glm::vec4(
            glm::normalize(cpuConfig.emitDirection),
            0.5f  // Default spread angle (radians)
        );
        gpuConfig.startColor = cpuConfig.startColor;
        gpuConfig.spawnRate = cpuConfig.spawnRate;
        gpuConfig.lifetime = cpuConfig.lifetime;
        gpuConfig.startSize = cpuConfig.startSize;
        gpuConfig.startSpeed = cpuConfig.startSpeed;
        gpuConfig.maxParticles = maxParticles;
        gpuConfig.seed = seed;
        gpuConfig.deltaTime = deltaTime;

        gpuConfig.modifierFlags = 0;
        gpuConfig.colorStart = cpuConfig.startColor;
        gpuConfig.colorEnd = cpuConfig.startColor;
        gpuConfig.sizeStartMult = 1.0f;
        gpuConfig.sizeEndMult = 1.0f;
        gpuConfig.speedStartMult = 1.0f;
        gpuConfig.speedEndMult = 1.0f;
        gpuConfig.angularVelocity = 0.0f;

        for (const auto& modifier : cpuConfig.modifiers.modifiers)
        {
            std::visit([&gpuConfig](const auto& mod) {
                using T = std::decay_t<decltype(mod)>;
                if constexpr (std::is_same_v<T, ::vfx::ColorOverLifetimeConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ModifierFlags::ColorOverLifetime;
                    gpuConfig.colorStart = mod.gradient.evaluate(0.0f);
                    gpuConfig.colorEnd = mod.gradient.evaluate(1.0f);
                }
                else if constexpr (std::is_same_v<T, ::vfx::SizeOverLifetimeConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ModifierFlags::SizeOverLifetime;
                    gpuConfig.sizeStartMult = mod.curve.evaluate(0.0f);
                    gpuConfig.sizeEndMult = mod.curve.evaluate(1.0f);
                }
                else if constexpr (std::is_same_v<T, ::vfx::SpeedOverLifetimeConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ModifierFlags::SpeedOverLifetime;
                    gpuConfig.speedStartMult = mod.curve.evaluate(0.0f);
                    gpuConfig.speedEndMult = mod.curve.evaluate(1.0f);
                }
                else if constexpr (std::is_same_v<T, ::vfx::RotationOverLifetimeConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ModifierFlags::RotationOverLifetime;
                    gpuConfig.angularVelocity = glm::radians(mod.curve.evaluate(0.0f));
                }
                else if constexpr (std::is_same_v<T, ::vfx::GlowOverLifetimeConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ModifierFlags::GlowOverLifetime;
                }
            }, modifier);
        }

        gpuConfig.gravityDir = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
        gpuConfig.windDir = glm::vec4(0.0f);
        gpuConfig.windNoise = glm::vec4(0.0f);
        gpuConfig.turbulence = glm::vec4(0.0f);
        gpuConfig.vortexAxis = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        gpuConfig.vortexCenter = glm::vec4(0.0f);

        for (const auto& force : cpuConfig.forces.forces)
        {
            std::visit([&gpuConfig](const auto& f) {
                using T = std::decay_t<decltype(f)>;
                if constexpr (std::is_same_v<T, ::vfx::GravityForceConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ForceFlags::Gravity;
                    gpuConfig.gravityDir = glm::vec4(glm::normalize(f.direction), f.strength);
                }
                else if constexpr (std::is_same_v<T, ::vfx::WindForceConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ForceFlags::Wind;
                    gpuConfig.windDir = glm::vec4(f.direction, f.strength);
                    gpuConfig.windNoise = glm::vec4(f.noiseStrength, f.noiseFrequency, 0.0f, 0.0f);
                }
                else if constexpr (std::is_same_v<T, ::vfx::TurbulenceForceConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ForceFlags::Turbulence;
                    gpuConfig.turbulence = glm::vec4(f.strength, f.frequency, f.scrollSpeed, static_cast<float>(f.octaves));
                }
                else if constexpr (std::is_same_v<T, ::vfx::VortexForceConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ForceFlags::Vortex;
                    gpuConfig.vortexAxis = glm::vec4(glm::normalize(f.axis), f.strength);
                    gpuConfig.vortexCenter = glm::vec4(f.center, f.radialPull);
                }
            }, force);
        }

        gpuConfig.shapeDimensions = cpuConfig.shape.dimensions;
        gpuConfig.shapeFlags = 0;

        switch (cpuConfig.shape.type)
        {
        case ::vfx::ShapeType::Sphere:
            gpuConfig.shapeFlags |= render::vfx::ShapeFlags::ShapeSphere;
            break;
        case ::vfx::ShapeType::Cone:
            gpuConfig.shapeFlags |= render::vfx::ShapeFlags::ShapeCone;
            break;
        case ::vfx::ShapeType::Box:
            gpuConfig.shapeFlags |= render::vfx::ShapeFlags::ShapeBox;
            break;
        case ::vfx::ShapeType::Torus:
            gpuConfig.shapeFlags |= render::vfx::ShapeFlags::ShapeTorus;
            break;
        case ::vfx::ShapeType::Point:
        default:
            break;
        }

        if (cpuConfig.shape.emitFrom == ::vfx::EmitFrom::Surface)
        {
            gpuConfig.shapeFlags |= render::vfx::ShapeFlags::EmitFromSurface;
        }

        if (cpuConfig.shape.randomDirection)
        {
            gpuConfig.shapeFlags |= render::vfx::ShapeFlags::RandomDirection;
        }

        gpuConfig.flipbookColumns = static_cast<float>(cpuConfig.flipbookColumns);
        gpuConfig.flipbookRows = static_cast<float>(cpuConfig.flipbookRows);
        gpuConfig.flipbookFrameRate = cpuConfig.flipbookFrameRate;
        if (cpuConfig.flipbookRandomStart)
        {
            gpuConfig.modifierFlags |= render::vfx::FlipbookFlags::RandomStart;
        }

        gpuConfig.renderMode = static_cast<uint32_t>(cpuConfig.renderMode);
        gpuConfig.softParticleDistance = cpuConfig.softParticleDistance;
        gpuConfig.stretchMultiplier = cpuConfig.stretchMultiplier;

        gpuConfig.drawIndexCount = 6;

        gpuConfig.maxTrailPoints = cpuConfig.maxTrailPoints;
        gpuConfig.ribbonWidth = cpuConfig.ribbonWidth;
        gpuConfig.ribbonMinDistance = cpuConfig.ribbonMinDistance;

        gpuConfig.uvScrollSpeedU = cpuConfig.uvScrollSpeedU;
        gpuConfig.uvScrollSpeedV = cpuConfig.uvScrollSpeedV;

        gpuConfig.eventFlags = cpuConfig.events.toEventFlags();
        gpuConfig.lifetimeThreshold = cpuConfig.events.lifetimeThreshold;

        // Collision response properties (colliderCount set separately from scene query)
        gpuConfig.collisionBounce = cpuConfig.collisionBounce;
        gpuConfig.collisionFriction = cpuConfig.collisionFriction;
        gpuConfig.collisionLifetimeLoss = cpuConfig.collisionLifetimeLoss;
        gpuConfig.terrainCollisionEnabled = cpuConfig.collisionEnabled ? 1u : 0u;

        return gpuConfig;
    }

    render::vfx::GPUEmitterState VFXSceneRenderer::toGPUState(
        const VFXRuntimeInstance& instance) const
    {
        render::vfx::GPUEmitterState gpuState{};
        gpuState.worldTransform = instance.worldTransform;
        gpuState.particleOffset = instance.gpuParticleOffset;
        gpuState.maxParticles = instance.gpuParticleCount;
        gpuState.activeCount = 0;
        gpuState.spawnThisFrame = 0;
        gpuState.spawnAccumulator = instance.spawnAccumulator;
        gpuState.flags = 0;
        if (instance.active)
        {
            gpuState.flags |= render::vfx::EmitterFlags::Playing;
        }
        if (instance.loop)
        {
            gpuState.flags |= render::vfx::EmitterFlags::Looping;
        }
        gpuState.spawnCounter = 0;
        gpuState.padding = 0;
        return gpuState;
    }

    void VFXSceneRenderer::recordComputeCommands(vk::CommandBuffer cmd)
    {
        if (!initialized || !gpuDrivenEnabled)
        {
            return;
        }

        if (!gpuComputePipeline || !gpuComputePipeline->isInitialized() || !gpuBufferManager)
        {
            return;
        }

        uint32_t activeGPUEmitters = 0;
        for (const auto& [id, instance] : instances)
        {
            if (instance.gpuDriven && instance.active)
            {
                activeGPUEmitters++;
            }
        }

        if (activeGPUEmitters == 0)
        {
            return;
        }

        gpuComputePipeline->insertBarriersBeforeTransfer(
            cmd,
            gpuBufferManager->getStateBuffer(),
            gpuBufferManager->getDrawCommandBuffer(),
            gpuBufferManager->getParticleBuffer()
        );

        gpuBufferManager->clearParticleBufferIfNeeded(cmd);

        gpuBufferManager->uploadStateBuffer(cmd);

        gpuBufferManager->clearDrawCommands(cmd);

        gpuComputePipeline->insertTransferToTransferBarrier(
            cmd,
            gpuBufferManager->getStateBuffer()
        );

        gpuBufferManager->resetAllActiveCounts(cmd);
        gpuBufferManager->clearEventBuffer(cmd);

        gpuComputePipeline->insertBarriersBeforeCompute(
            cmd,
            gpuBufferManager->getStateBuffer(),
            gpuBufferManager->getDrawCommandBuffer(),
            gpuBufferManager->getParticleBuffer(),
            gpuBufferManager->getEventBuffer()
        );

        for (const auto& [id, instance] : instances)
        {
            if (!instance.gpuDriven || !instance.active)
            {
                continue;
            }

            // Skip compute dispatch for emitters fully outside the frustum
            if (!isEmitterInFrustum(instance))
            {
                continue;
            }

            gpuComputePipeline->dispatch(
                cmd,
                instance.gpuEmitterIndex,
                instance.gpuParticleCount,
                frameNumber,
                gpuBufferManager->getMaxEmitters()
            );
        }

        gpuComputePipeline->insertBarriersAfterCompute(cmd, gpuBufferManager->getBufferSet());

        gpuBufferManager->copyEventBufferToReadback(cmd);
    }

    void VFXSceneRenderer::recordGPUDrawCommands(vk::CommandBuffer cmd)
    {
        if (!gpuRenderPipeline || !gpuRenderPipeline->isInitialized() || !gpuBufferManager)
        {
            return;
        }

        uint32_t activeGPUEmitters = 0;
        for (const auto& [id, instance] : instances)
        {
            if (instance.gpuDriven && instance.active)
            {
                if (distanceCullingEnabled && maxVFXDistSq > 0.0f)
                {
                    glm::vec3 emitterPos = glm::vec3(instance.worldTransform[3]);
                    glm::vec3 diff = emitterPos - currentCameraPos;
                    float distSq = glm::dot(diff, diff);
                    if (distSq > maxVFXDistSq)
                    {
                        continue;
                    }
                }
                activeGPUEmitters++;
            }
        }

        if (activeGPUEmitters == 0)
        {
            return;
        }

        gpuRenderPipeline->recordCommandsInline(
            cmd,
            gpuBufferManager->getDrawCommandBuffer(),
            gpuBufferManager->getMaxEmitters()
        );

        if (gpuMeshPipeline && gpuMeshPipeline->isInitialized())
        {
            gpuMeshPipeline->recordCommandsInline(
                cmd,
                gpuBufferManager->getDrawCommandBuffer(),
                gpuBufferManager->getMaxEmitters()
            );
        }

        if (gpuRibbonPipeline && gpuRibbonPipeline->isInitialized())
        {
            gpuRibbonPipeline->recordCommandsInline(
                cmd,
                gpuBufferManager->getDrawCommandBuffer(),
                gpuBufferManager->getMaxEmitters()
            );
        }
    }

    void VFXSceneRenderer::processEvents()
    {
        if (lastFrameEventCount == 0)
        {
            return;
        }

        for (uint32_t i = 0; i < lastFrameEventCount; ++i)
        {
            const auto& event = lastFrameEvents[i];

            // Find parent instance by emitter index via reverse map
            auto mapIt = emitterIndexToInstanceId.find(event.emitterIndex);
            if (mapIt == emitterIndexToInstanceId.end())
                continue;

            VFXInstanceId parentId = mapIt->second;
            auto instIt = instances.find(parentId);
            if (instIt == instances.end())
                continue;

            const VFXRuntimeInstance* parentInstance = &instIt->second;

            // Prevent recursion: skip events from sub-emitter instances
            bool isSubEmitter = false;
            for (const auto& sub : activeSubEmitters)
            {
                if (sub.subId == parentId)
                {
                    isSubEmitter = true;
                    break;
                }
            }
            if (isSubEmitter)
            {
                continue;
            }

            // Resolve .vfx path from event type
            std::string vfxPath;
            const auto& eventConfig = parentInstance->config.events;

            switch (event.eventType)
            {
            case 0: // OnSpawn
                if (eventConfig.onSpawnEnabled) vfxPath = eventConfig.onSpawnVFXPath;
                break;
            case 1: // OnDeath
                if (eventConfig.onDeathEnabled) vfxPath = eventConfig.onDeathVFXPath;
                break;
            case 2: // OnCollision
                if (eventConfig.onCollisionEnabled) vfxPath = eventConfig.onCollisionVFXPath;
                break;
            case 3: // OnLifetimeThreshold
                if (eventConfig.onLifetimeThresholdEnabled) vfxPath = eventConfig.onLifetimeThresholdVFXPath;
                break;
            }

            if (vfxPath.empty())
            {
                continue;
            }

            // Count existing sub-emitters for this parent
            uint32_t parentSubCount = 0;
            for (const auto& sub : activeSubEmitters)
            {
                if (sub.parentId == parentId && !sub.finished)
                {
                    parentSubCount++;
                }
            }

            if (parentSubCount >= MAX_SUB_EMITTERS_PER_PARENT)
            {
                continue;
            }

            // Create sub-emitter instance at event position
            VFXRuntimeParams subParams;
            subParams.vfxAssetPath = vfxPath;
            subParams.worldTransform = glm::translate(glm::mat4(1.0f),
                glm::vec3(event.position.x, event.position.y, event.position.z));
            subParams.loop = false;

            VFXInstanceId subId = createInstance(subParams);
            if (subId != 0)
            {
                playInstance(subId);

                SubEmitterInstance subEmitter;
                subEmitter.parentId = parentId;
                subEmitter.subId = subId;
                subEmitter.lifetime = 0.0f;

                // Use the sub-emitter's configured lifetime as max lifetime
                auto subIt = instances.find(subId);
                if (subIt != instances.end())
                {
                    subEmitter.maxLifetime = subIt->second.config.lifetime * 2.0f;
                }

                activeSubEmitters.push_back(subEmitter);
            }

            // Publish CQRS notification for external systems
            services::events::vfxruntime::VFXParticleEventNotification notification;
            notification.eventType = event.eventType;
            notification.position = glm::vec3(event.position.x, event.position.y, event.position.z);
            notification.velocity = glm::vec3(event.velocity.x, event.velocity.y, event.velocity.z);
            notification.emitterIndex = event.emitterIndex;
            notification.parentInstanceId = parentId;
            notification.entityId = parentInstance->entityId;
            notification.vfxAssetPath = vfxPath;
            events::EventDispatcher::instance().publish(notification);
        }
    }

    void VFXSceneRenderer::extractFrustumPlanes(const glm::mat4& viewProj)
    {
        // Extract 6 frustum planes from view-projection matrix (Gribb-Hartmann method)
        const auto& m = viewProj;
        // Left
        frustumPlanes[0] = glm::vec4(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0], m[3][3] + m[3][0]);
        // Right
        frustumPlanes[1] = glm::vec4(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0], m[3][3] - m[3][0]);
        // Bottom
        frustumPlanes[2] = glm::vec4(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1], m[3][3] + m[3][1]);
        // Top
        frustumPlanes[3] = glm::vec4(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1], m[3][3] - m[3][1]);
        // Near
        frustumPlanes[4] = glm::vec4(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2], m[3][3] + m[3][2]);
        // Far
        frustumPlanes[5] = glm::vec4(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2], m[3][3] - m[3][2]);

        // Normalize planes
        for (int i = 0; i < 6; ++i)
        {
            float len = glm::length(glm::vec3(frustumPlanes[i]));
            if (len > 0.0f)
            {
                frustumPlanes[i] /= len;
            }
        }
        frustumPlanesValid = true;
    }

    bool VFXSceneRenderer::isEmitterInFrustum(const VFXRuntimeInstance& instance) const
    {
        if (!frustumPlanesValid)
            return true;

        // Camera-relative emitters are always visible
        if (instance.cameraRelative)
            return true;

        glm::vec3 emitterPos = glm::vec3(instance.worldTransform[3]);

        // Conservative bounding sphere radius based on lifetime, speed, and shape dimensions
        float maxDim = glm::max(glm::max(
            std::abs(instance.config.shape.dimensions.x),
            std::abs(instance.config.shape.dimensions.y)),
            std::abs(instance.config.shape.dimensions.z));
        float radius = std::max(
            instance.config.lifetime * instance.config.startSpeed,
            maxDim) + 5.0f; // margin to avoid pop-in

        // Test sphere against all 6 planes
        for (int i = 0; i < 6; ++i)
        {
            float dist = glm::dot(glm::vec3(frustumPlanes[i]), emitterPos) + frustumPlanes[i].w;
            if (dist < -radius)
            {
                return false;
            }
        }
        return true;
    }

    void VFXSceneRenderer::updateInstanceLOD(VFXRuntimeInstance& instance) const
    {
        if (instance.cameraRelative)
        {
            instance.currentLOD = 0;
            instance.lodSpawnMultiplier = 1.0f;
            return;
        }

        glm::vec3 emitterPos = glm::vec3(instance.worldTransform[3]);
        float dist = glm::distance(emitterPos, currentCameraPos);

        // Apply per-emitter LOD bias (shifts thresholds)
        dist -= instance.lodBias;
        dist = std::max(dist, 0.0f);

        float multiplier = 1.0f;
        uint8_t lod = 0;

        if (dist < LOD0_DIST)
        {
            lod = 0;
            multiplier = 1.0f;
        }
        else if (dist < LOD1_DIST)
        {
            lod = 1;
            // Smooth transition from LOD0 to LOD1
            float t = std::clamp((dist - LOD0_DIST) / LOD_TRANSITION_ZONE, 0.0f, 1.0f);
            multiplier = glm::mix(1.0f, 0.5f, t);
        }
        else if (dist < LOD2_DIST)
        {
            lod = 2;
            float t = std::clamp((dist - LOD1_DIST) / LOD_TRANSITION_ZONE, 0.0f, 1.0f);
            multiplier = glm::mix(0.5f, 0.25f, t);
        }
        else
        {
            lod = 3;
            multiplier = 0.0f; // No new spawns
        }

        instance.currentLOD = lod;
        instance.lodSpawnMultiplier = multiplier;
    }

    VFXInstanceId VFXSceneRenderer::findLowestPriorityInstance(services::VFXEmitterPriority belowPriority) const
    {
        VFXInstanceId worstId = 0;
        services::VFXEmitterPriority worstPriority = services::VFXEmitterPriority::Critical;

        for (const auto& [id, instance] : instances)
        {
            if (!instance.gpuDriven || !instance.active)
                continue;
            // Never evict Critical emitters
            if (instance.priority == services::VFXEmitterPriority::Critical)
                continue;
            if (static_cast<uint8_t>(instance.priority) > static_cast<uint8_t>(worstPriority))
            {
                worstPriority = instance.priority;
                worstId = id;
            }
        }

        // Only return if found priority is strictly lower (higher numeric value) than requested
        if (worstId != 0 && static_cast<uint8_t>(worstPriority) > static_cast<uint8_t>(belowPriority))
        {
            return worstId;
        }
        return 0;
    }

    void VFXSceneRenderer::cleanupFinishedSubEmitters(float deltaTime)
    {
        for (auto& sub : activeSubEmitters)
        {
            if (sub.finished)
            {
                continue;
            }

            sub.lifetime += deltaTime;

            if (sub.lifetime >= sub.maxLifetime)
            {
                sub.finished = true;
                destroyInstance(sub.subId);
            }
        }

        activeSubEmitters.erase(
            std::remove_if(activeSubEmitters.begin(), activeSubEmitters.end(),
                [](const SubEmitterInstance& s) { return s.finished; }),
            activeSubEmitters.end());
    }
}
