#include "VFXSceneRenderer.hpp"
#include "../render/vfx/compute/GPUVFXBufferManager.hpp"
#include "../render/vfx/compute/GPUVFXComputePipeline.hpp"
#include "../render/vfx/scene/VFXSceneGPUPipeline.hpp"
#include "../render/vfx/mesh/VFXMeshGPUPipeline.hpp"
#include "../render/vfx/ribbon/VFXRibbonGPUPipeline.hpp"
#include "../render/vfx/distortion/VFXDistortionPipeline.hpp"
#include "../render/vfx/bindless/VFXBindlessTextures.hpp"
#include "../render/vfx/particle/VFXEmitterPool.hpp"
#include "../render/vfx/particle/VFXParticleSystem.hpp"
#include "../render/vfx/lut/VFXLUTBaker.hpp"
#include "../render/mesh/MeshGPUCache.hpp"
#include "../core/RenderManager.hpp"
#include "vfx/VFXModifierTypes.hpp"
#include "vfx/VFXForceTypes.hpp"
#include "vfx/VFXKillVolume.hpp"
#include "vfx/VFXRuntimeDiagnostics.hpp"
#include "vfx/VFXShapeTypes.hpp"
#include "vfx/VFXOrientationMode.hpp"
#include "print/Log.hpp"
#include <random>
#include <type_traits>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace controllers
{
    bool VFXSceneRenderer::initGPUMode(vk::Format colorFormat, vk::Format depthFormat)
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

            // VK-1481: the shared bindless texture table must exist before any pipeline is created
            // (createPipeline appends its descriptor set layout).
            bindlessTextures = std::make_unique<render::vfx::VFXBindlessTextures>(device);
            bindlessTextures->init();
            bindlessTextures->setDeletionQueue(core::RenderManager::getGlobalDeletionQueue());

            gpuRenderPipeline = std::make_unique<render::vfx::VFXSceneGPUPipeline>(device, swapChain);
            if (hasLightingLayouts)
                gpuRenderPipeline->setLightingLayouts(cachedLightBufferLayout, cachedClusterGridLayout, cachedClusterLightGridLayout);
            gpuRenderPipeline->setBindlessTextures(bindlessTextures.get());
            gpuRenderPipeline->init(colorFormat, depthFormat);
            if (!gpuRenderPipeline->isInitialized())
            {
                vfLogError("Failed to initialize GPU VFX render pipeline");
                return false;
            }

            gpuMeshCache = std::make_unique<render::mesh::MeshGPUCache>(device);
            gpuMeshPipeline = std::make_unique<render::vfx::VFXMeshGPUPipeline>(device, swapChain, *gpuMeshCache);
            if (hasLightingLayouts)
                gpuMeshPipeline->setLightingLayouts(cachedLightBufferLayout, cachedClusterGridLayout, cachedClusterLightGridLayout);
            gpuMeshPipeline->setBindlessTextures(bindlessTextures.get());
            gpuMeshPipeline->init(colorFormat, depthFormat);
            if (!gpuMeshPipeline->isInitialized())
            {
                vfLogError("Failed to initialize GPU VFX mesh pipeline");
                return false;
            }

            gpuRibbonPipeline = std::make_unique<render::vfx::VFXRibbonGPUPipeline>(device, swapChain);
            if (hasLightingLayouts)
                gpuRibbonPipeline->setLightingLayouts(cachedLightBufferLayout, cachedClusterGridLayout, cachedClusterLightGridLayout);
            gpuRibbonPipeline->setBindlessTextures(bindlessTextures.get());
            gpuRibbonPipeline->init(colorFormat, depthFormat);
            if (!gpuRibbonPipeline->isInitialized())
            {
                vfLogError("Failed to initialize GPU VFX ribbon pipeline");
                return false;
            }

            gpuComputePipeline->updateDescriptors(gpuBufferManager->getBufferSet());

            gpuRenderPipeline->updateParticleBuffer(
                gpuBufferManager->getParticleBuffer(), gpuBufferManager->getParticleBufferSize());
            gpuRenderPipeline->updateConfigBuffer(
                gpuBufferManager->getConfigBuffer(), gpuBufferManager->getConfigBufferSize());

            gpuMeshPipeline->updateParticleBuffer(
                gpuBufferManager->getParticleBuffer(), gpuBufferManager->getParticleBufferSize());
            gpuMeshPipeline->updateConfigBuffer(
                gpuBufferManager->getConfigBuffer(), gpuBufferManager->getConfigBufferSize());

            gpuRibbonPipeline->updateParticleBuffer(
                gpuBufferManager->getParticleBuffer(), gpuBufferManager->getParticleBufferSize());
            gpuRibbonPipeline->updateConfigBuffer(
                gpuBufferManager->getConfigBuffer(), gpuBufferManager->getConfigBufferSize());
            gpuRibbonPipeline->updateRibbonBuffers(
                gpuBufferManager->getRibbonRingBuffer(), gpuBufferManager->getRibbonRingBufferSize(),
                gpuBufferManager->getRibbonHeadBuffer(), gpuBufferManager->getRibbonHeadBufferSize());
            gpuRibbonPipeline->updateLutBuffer( // VK-1474: ribbon width curve / tail gradient
                gpuBufferManager->getLUTBuffer(), gpuBufferManager->getLUTBufferSize());

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
        if (emitterPool) { emitterPool->reset(); emitterPool.reset(); }
        if (gpuDistortionPipeline) { gpuDistortionPipeline->cleanup(); gpuDistortionPipeline.reset(); }
        if (gpuRibbonPipeline) { gpuRibbonPipeline->cleanup(); gpuRibbonPipeline.reset(); }
        if (gpuMeshPipeline) { gpuMeshPipeline->cleanup(); gpuMeshPipeline.reset(); }
        if (gpuMeshCache) { gpuMeshCache->unloadAllMeshes(); gpuMeshCache.reset(); }
        if (gpuRenderPipeline) { gpuRenderPipeline->cleanup(); gpuRenderPipeline.reset(); }
        if (gpuComputePipeline) { gpuComputePipeline->cleanup(); gpuComputePipeline.reset(); }
        // VK-1481: tear down the shared bindless table AFTER all pipelines that reference it.
        if (bindlessTextures) { bindlessTextures->cleanup(); bindlessTextures.reset(); }
        if (gpuBufferManager) { gpuBufferManager->cleanup(); gpuBufferManager.reset(); }
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
            return;

        if (sceneColliderCount > 0)
            gpuBufferManager->updateSceneColliders(sceneColliders, sceneColliderCount);

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
        lastFrameRawEventCount = gpuBufferManager->getLastRawEventCount();
        if (lastFrameRawEventCount > render::vfx::GPUVFXConstants::MAX_VFX_EVENTS_PER_FRAME)
        {
            vfx::VFXRuntimeDiagnostics::instance().report(
                "VFX events",
                "event buffer saturated: >256/frame, child spawns dropped");
        }
        processEvents();
        cleanupFinishedSubEmitters(deltaTime);

        for (auto& [id, instance] : instances)
        {
            if (!instance.gpuDriven && instance.particleSystem && instance.active)
                instance.particleSystem->update(deltaTime);
        }

        static std::random_device rd;
        static std::mt19937 gen(rd());
        // Burst probability RNG only — emitter seed is now the stable per-instance value
        // (VK-1451). Burst jitter stays frame-random (accepted GPU micro-nondeterminism).
        std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

        for (auto& [id, instance] : instances)
        {
            if (!instance.gpuDriven || !instance.active)
                continue;

            updateInstanceLOD(instance);

            if (instance.cameraRelative)
                instance.worldTransform = glm::translate(glm::mat4(1.0f), currentCameraPos);

            float effectiveDt = deltaTime;
            if (instance.firstFrame)
            {
                effectiveDt = std::min(deltaTime, 1.0f / 30.0f);
                instance.firstFrame = false;
                instance.prevWorldTransform = instance.worldTransform;
            }

            if (effectiveDt > 0.0f)
            {
                glm::vec3 currentPos = glm::vec3(instance.worldTransform[3]);
                glm::vec3 prevPos = glm::vec3(instance.prevWorldTransform[3]);
                instance.emitterVelocity = (currentPos - prevPos) / effectiveDt;
            }

            instance.emissionTime += effectiveDt;

            uint32_t spawnThisFrame = 0;
            bool canSpawn = instance.loop || (instance.emissionTime < instance.config.lifetime);

            if (instance.active && canSpawn)
            {
                float lodAdjustedRate = instance.config.spawnRate * instance.lodSpawnMultiplier;
                instance.spawnAccumulator += lodAdjustedRate * effectiveDt;
                spawnThisFrame = static_cast<uint32_t>(instance.spawnAccumulator);
                instance.spawnAccumulator -= static_cast<float>(spawnThisFrame);

                if (!instance.config.bursts.empty())
                {
                    float prevEmissionTime = instance.emissionTime - effectiveDt;
                    uint32_t burstSpawns = ::vfx::evaluateBurstSpawns(
                        instance.config.bursts, prevEmissionTime, instance.emissionTime,
                        [&dist01]() { return dist01(gen); });
                    burstSpawns = static_cast<uint32_t>(
                        static_cast<float>(burstSpawns) * instance.lodSpawnMultiplier);

                    if (burstSpawns > instance.gpuParticleCount && !instance.burstClampWarned)
                    {
                        vfLogWarning("VFX instance {}: burst of {} particles exceeds emitter pool size {} - clamped",
                                     id, burstSpawns, instance.gpuParticleCount);
                        instance.burstClampWarned = true;
                    }

                    spawnThisFrame += burstSpawns;
                }
            }

            // VK-1451: feed the stable per-instance seed (set once at creation) instead
            // of re-randomizing every frame, so an explicitly-seeded instance reproduces
            // its emission schedule. The shader still folds particleIdx/frameNumber into
            // the per-particle RNG, so visuals stay varied without being random per frame.
            auto gpuConfig = toGPUConfig(instance.config, effectiveDt, instance.gpuParticleCount, instance.seed);

            gpuConfig.colliderCount = (instance.config.collisionEnabled && instance.currentLOD == 0)
                                          ? sceneColliderCount : 0;

            if (instance.currentLOD >= 2)
                gpuConfig.softParticleDistance = 0.0f;

            gpuConfig.spawnRate = instance.config.spawnRate * instance.lodSpawnMultiplier;

            if (instance.config.renderMode == render::vfx::VFXRenderMode::MeshParticle && gpuMeshPipeline)
                gpuConfig.drawIndexCount = gpuMeshPipeline->getEmitterMeshIndexCount(instance.gpuEmitterIndex);

            render::vfx::VFXLUTBaker::RibbonLUTInputs ribbonLutInputs;
            if (instance.config.hasRibbonWidthCurve)
                ribbonLutInputs.widthCurve = &instance.config.ribbonWidthCurve;
            if (instance.config.hasRibbonTailGradient)
                ribbonLutInputs.tailGradient = &instance.config.ribbonTailGradient;
            auto lutResult = render::vfx::VFXLUTBaker::bake(instance.config.modifiers, ribbonLutInputs);
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

            instance.prevWorldTransform = instance.worldTransform;
        }
    }

    void VFXSceneRenderer::initDistortion(vk::Format colorFormat, vk::Format depthFormat)
    {
        if (!gpuBufferManager || gpuDistortionPipeline)
            return;

        gpuDistortionPipeline = std::make_unique<render::vfx::VFXDistortionPipeline>(device, swapChain);
        gpuDistortionPipeline->setBindlessTextures(bindlessTextures.get());
        gpuDistortionPipeline->init(colorFormat, depthFormat);

        gpuDistortionPipeline->updateParticleBuffer(
            gpuBufferManager->getParticleBuffer(), gpuBufferManager->getParticleBufferSize());
        gpuDistortionPipeline->updateConfigBuffer(
            gpuBufferManager->getConfigBuffer(), gpuBufferManager->getConfigBufferSize());

        // Set up existing distortion emitters
        for (const auto& [id, instance] : instances)
        {
            if (instance.gpuDriven && instance.config.distortionEnabled)
            {
                gpuDistortionPipeline->setEmitterDistortionTexture(
                    instance.gpuEmitterIndex, instance.config.distortionTexturePath);
                gpuDistortionPipeline->setEmitterDistortionConfig(
                    instance.gpuEmitterIndex, instance.config.distortionStrength);
            }
        }
    }

    void VFXSceneRenderer::recreateDistortion(vk::Format colorFormat, vk::Format depthFormat)
    {
        if (gpuDistortionPipeline)
        {
            gpuDistortionPipeline->cleanup();
            gpuDistortionPipeline.reset();
        }
        initDistortion(colorFormat, depthFormat);
    }

    render::vfx::GPUEmitterConfig VFXSceneRenderer::toGPUConfig(
        const render::vfx::VFXEmitterConfig& cpuConfig,
        float deltaTime,
        uint32_t maxParticles,
        uint32_t seed) const
    {
        render::vfx::GPUEmitterConfig gpuConfig{};
        gpuConfig.emitDirection = glm::vec4(glm::normalize(cpuConfig.emitDirection), cpuConfig.coneSpread);
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
                else if constexpr (std::is_same_v<T, ::vfx::SizeBySpeedConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ModifierFlags::SizeBySpeed;
                    gpuConfig.modifierSpeedRanges.x = mod.speedMin;
                    gpuConfig.modifierSpeedRanges.y = mod.speedMax;
                }
                else if constexpr (std::is_same_v<T, ::vfx::ColorBySpeedConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ModifierFlags::ColorBySpeed;
                    gpuConfig.modifierSpeedRanges.z = mod.speedMin;
                    gpuConfig.modifierSpeedRanges.w = mod.speedMax;
                }
            }, modifier);
        }

        gpuConfig.gravityDir = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
        gpuConfig.windDir = glm::vec4(0.0f);
        gpuConfig.windNoise = glm::vec4(0.0f);
        gpuConfig.turbulence = glm::vec4(0.0f);
        gpuConfig.vortexAxis = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        gpuConfig.vortexCenter = glm::vec4(0.0f);
        gpuConfig.attractorParams = glm::vec4(0.0f);
        gpuConfig.dragAttractorExtra = glm::vec4(0.0f);
        gpuConfig.curlNoiseParams = glm::vec4(0.0f);
        gpuConfig.killVolumeParams0 = glm::vec4(0.0f);
        gpuConfig.killVolumeParams1 = glm::vec4(0.0f);

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
                else if constexpr (std::is_same_v<T, ::vfx::DragForceConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ForceFlags::Drag;
                    gpuConfig.dragAttractorExtra.x = f.linearCoeff;
                    gpuConfig.dragAttractorExtra.y = f.quadraticCoeff;
                }
                else if constexpr (std::is_same_v<T, ::vfx::PointAttractorForceConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ForceFlags::Attractor;
                    if (f.killAtCenter)
                        gpuConfig.modifierFlags |= render::vfx::ForceFlags::AttractorKill;
                    gpuConfig.attractorParams = glm::vec4(f.position, f.strength);
                    gpuConfig.dragAttractorExtra.z = f.radius;
                    gpuConfig.dragAttractorExtra.w = f.falloff;
                }
                else if constexpr (std::is_same_v<T, ::vfx::CurlNoiseForceConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ForceFlags::CurlNoise;
                    gpuConfig.curlNoiseParams = glm::vec4(f.strength, f.frequency, f.scrollSpeed, static_cast<float>(f.octaves));
                }
                else if constexpr (std::is_same_v<T, ::vfx::KillVolumeForceConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ForceFlags::KillVolume;
                    gpuConfig.killVolumeParams0 = glm::vec4(f.center, std::max(f.radius, 0.0f));

                    glm::vec3 axisOrExtents(0.0f);
                    if (f.shape == ::vfx::KillVolumeShape::Plane)
                        axisOrExtents = ::vfx::sanitizeKillVolumeNormal(f.normal);
                    else if (f.shape == ::vfx::KillVolumeShape::Box)
                        axisOrExtents = ::vfx::sanitizeKillVolumeHalfExtents(f.halfExtents);

                    uint32_t packed = static_cast<uint32_t>(f.shape);
                    if (f.invert)
                        packed |= 4u;
                    if (f.space == ::vfx::ForceSpace::Local)
                        packed |= 8u;

                    gpuConfig.killVolumeParams1 = glm::vec4(axisOrExtents, static_cast<float>(packed));
                }
            }, force);
        }

        gpuConfig.shapeDimensions = cpuConfig.shape.dimensions;
        gpuConfig.shapeFlags = 0;

        switch (cpuConfig.shape.type)
        {
        case ::vfx::ShapeType::Sphere: gpuConfig.shapeFlags |= render::vfx::ShapeFlags::ShapeSphere; break;
        case ::vfx::ShapeType::Cone:   gpuConfig.shapeFlags |= render::vfx::ShapeFlags::ShapeCone; break;
        case ::vfx::ShapeType::Box:    gpuConfig.shapeFlags |= render::vfx::ShapeFlags::ShapeBox; break;
        case ::vfx::ShapeType::Torus:  gpuConfig.shapeFlags |= render::vfx::ShapeFlags::ShapeTorus; break;
        case ::vfx::ShapeType::Point:
        default: break;
        }

        if (cpuConfig.shape.emitFrom == ::vfx::EmitFrom::Surface)
            gpuConfig.shapeFlags |= render::vfx::ShapeFlags::EmitFromSurface;

        if (cpuConfig.shape.randomDirection)
            gpuConfig.shapeFlags |= render::vfx::ShapeFlags::RandomDirection;

        gpuConfig.flipbookColumns = static_cast<float>(cpuConfig.flipbookColumns);
        gpuConfig.flipbookRows = static_cast<float>(cpuConfig.flipbookRows);
        gpuConfig.flipbookFrameRate = cpuConfig.flipbookFrameRate;
        if (cpuConfig.flipbookRandomStart)
            gpuConfig.modifierFlags |= render::vfx::FlipbookFlags::RandomStart;
        if (cpuConfig.flipbookFrameBlend)
            gpuConfig.modifierFlags |= render::vfx::FlipbookFlags::FrameBlend;

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
        gpuConfig.collisionBounce = cpuConfig.collisionBounce;
        gpuConfig.collisionFriction = cpuConfig.collisionFriction;
        gpuConfig.collisionLifetimeLoss = cpuConfig.collisionLifetimeLoss;
        gpuConfig.terrainCollisionEnabled = cpuConfig.collisionEnabled ? 1u : 0u;
        gpuConfig.lightingInfluence = cpuConfig.lightingInfluence;
        gpuConfig.normalMode = cpuConfig.normalMode;
        gpuConfig.ambientAmount = cpuConfig.ambientAmount;
        gpuConfig.distortionEnabled = cpuConfig.distortionEnabled ? 1u : 0u;
        gpuConfig.distortionStrength = cpuConfig.distortionStrength;
        gpuConfig.sizeVariance = cpuConfig.sizeVariance;
        gpuConfig.lifetimeVariance = cpuConfig.lifetimeVariance;
        gpuConfig.speedVariance = cpuConfig.speedVariance;
        gpuConfig.rotationVariance = cpuConfig.rotationVariance;
        gpuConfig.angularVelocityVariance = cpuConfig.angularVelocityVariance;
        gpuConfig.colorValueVariance = cpuConfig.colorValueVariance;
        gpuConfig.alphaVariance = cpuConfig.alphaVariance;
        gpuConfig.emissiveIntensity = cpuConfig.emissiveIntensity;

        // VK-1476 mesh orientation. Guard a zero/degenerate axis so the shader's
        // normalize can never produce NaN (axis-lock about a valid default axis).
        gpuConfig.meshOrientationMode = ::vfx::orientationModeToGpuValue(cpuConfig.meshOrientationMode);
        glm::vec3 orientAxis = cpuConfig.meshOrientationAxis;
        orientAxis = (glm::length(orientAxis) < 1e-6f) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                       : glm::normalize(orientAxis);
        gpuConfig.meshOrientationParams = glm::vec4(orientAxis, cpuConfig.meshOrientationSpinRate);

        return gpuConfig;
    }

    render::vfx::GPUEmitterState VFXSceneRenderer::toGPUState(
        const VFXRuntimeInstance& instance) const
    {
        render::vfx::GPUEmitterState gpuState{};
        gpuState.worldTransform = instance.worldTransform;
        gpuState.prevWorldTransform = instance.prevWorldTransform;
        gpuState.emitterVelocityAndInherit = instance.injectedEmitterVelocity.has_value()
            ? glm::vec4(instance.injectedEmitterVelocity.value(), 1.0f)
            : glm::vec4(instance.emitterVelocity, instance.config.inheritVelocityRatio);
        gpuState.particleOffset = instance.gpuParticleOffset;
        gpuState.maxParticles = instance.gpuParticleCount;
        gpuState.activeCount = 0;
        gpuState.spawnThisFrame = 0;
        gpuState.spawnAccumulator = instance.spawnAccumulator;
        gpuState.flags = 0;
        if (instance.active)
            gpuState.flags |= render::vfx::EmitterFlags::Playing;
        if (instance.loop)
            gpuState.flags |= render::vfx::EmitterFlags::Looping;
        gpuState.spawnCounter = 0;
        gpuState.padding = 0;
        return gpuState;
    }

    void VFXSceneRenderer::setLightingLayouts(
        vk::DescriptorSetLayout lightBufferLayout,
        vk::DescriptorSetLayout clusterGridLayout,
        vk::DescriptorSetLayout clusterLightGridLayout)
    {
        cachedLightBufferLayout = lightBufferLayout;
        cachedClusterGridLayout = clusterGridLayout;
        cachedClusterLightGridLayout = clusterLightGridLayout;
        hasLightingLayouts = lightBufferLayout && clusterGridLayout && clusterLightGridLayout;

        if (gpuRenderPipeline)
            gpuRenderPipeline->setLightingLayouts(lightBufferLayout, clusterGridLayout, clusterLightGridLayout);
        if (gpuMeshPipeline)
            gpuMeshPipeline->setLightingLayouts(lightBufferLayout, clusterGridLayout, clusterLightGridLayout);
        if (gpuRibbonPipeline)
            gpuRibbonPipeline->setLightingLayouts(lightBufferLayout, clusterGridLayout, clusterLightGridLayout);
    }

    void VFXSceneRenderer::updateLightingDescriptorSets(
        vk::DescriptorSet lightBufferSet,
        vk::DescriptorSet clusterGridSet,
        vk::DescriptorSet clusterLightGridSet)
    {
        if (gpuRenderPipeline)
            gpuRenderPipeline->updateLightingDescriptorSets(lightBufferSet, clusterGridSet, clusterLightGridSet);
        if (gpuMeshPipeline)
            gpuMeshPipeline->updateLightingDescriptorSets(lightBufferSet, clusterGridSet, clusterLightGridSet);
        if (gpuRibbonPipeline)
            gpuRibbonPipeline->updateLightingDescriptorSets(lightBufferSet, clusterGridSet, clusterLightGridSet);
    }
}
