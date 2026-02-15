#include "VFXSceneRenderer.hpp"
#include "../render/vfx/GPUVFXBufferManager.hpp"
#include "../render/vfx/GPUVFXComputePipeline.hpp"
#include "../render/vfx/VFXSceneGPUPipeline.hpp"
#include "../render/vfx/VFXParticleSystem.hpp"
#include "vfx/VFXModifierTypes.hpp"
#include "vfx/VFXForceTypes.hpp"
#include "vfx/VFXShapeTypes.hpp"
#include "print/Logger.hpp"
#include <random>
#include <type_traits>

namespace controllers
{
    bool VFXSceneRenderer::initGPUMode(vk::RenderPass renderPass)
    {
        try
        {
            gpuBufferManager = std::make_unique<render::vfx::GPUVFXBufferManager>(device);
            if (!gpuBufferManager->init())
            {
                loggerError("Failed to initialize GPU VFX buffer manager");
                return false;
            }

            gpuComputePipeline = std::make_unique<render::vfx::GPUVFXComputePipeline>(device);
            gpuComputePipeline->init();
            if (!gpuComputePipeline->isInitialized())
            {
                loggerError("Failed to initialize GPU VFX compute pipeline");
                return false;
            }

            gpuRenderPipeline = std::make_unique<render::vfx::VFXSceneGPUPipeline>(device, swapChain);
            gpuRenderPipeline->init(renderPass);
            if (!gpuRenderPipeline->isInitialized())
            {
                loggerError("Failed to initialize GPU VFX render pipeline");
                return false;
            }

            gpuComputePipeline->updateDescriptors(
                gpuBufferManager->getParticleBuffer(),
                gpuBufferManager->getConfigBuffer(),
                gpuBufferManager->getStateBuffer(),
                gpuBufferManager->getDrawCommandBuffer()
            );

            gpuRenderPipeline->updateParticleBuffer(
                gpuBufferManager->getParticleBuffer(),
                gpuBufferManager->getParticleBufferSize()
            );

            loggerInfo("GPU VFX mode initialized: {} max particles, {} max emitters",
                       gpuBufferManager->getMaxParticles(),
                       gpuBufferManager->getMaxEmitters());
            return true;
        }
        catch (const std::exception& e)
        {
            loggerError("Exception during GPU VFX initialization: {}", e.what());
            cleanupGPUMode();
            return false;
        }
    }

    void VFXSceneRenderer::cleanupGPUMode()
    {
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

    void VFXSceneRenderer::updateGPU(float deltaTime)
    {
        if (!gpuBufferManager)
        {
            return;
        }

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

            // Track emission time for looping control
            instance.emissionTime += deltaTime;

            uint32_t spawnThisFrame = 0;
            // Only spawn new particles if:
            // - looping is enabled, OR
            // - we haven't exceeded the emission duration (one lifetime cycle)
            bool canSpawn = instance.loop || (instance.emissionTime < instance.config.lifetime);

            if (instance.active && canSpawn)
            {
                instance.spawnAccumulator += instance.config.spawnRate * deltaTime;
                spawnThisFrame = static_cast<uint32_t>(instance.spawnAccumulator);
                instance.spawnAccumulator -= static_cast<float>(spawnThisFrame);
            }

            auto gpuConfig = toGPUConfig(
                instance.config,
                deltaTime,
                instance.gpuParticleCount,
                dist(gen)
            );
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
                    gpuConfig.colorStart = mod.startColor;
                    gpuConfig.colorEnd = mod.endColor;
                }
                else if constexpr (std::is_same_v<T, ::vfx::SizeOverLifetimeConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ModifierFlags::SizeOverLifetime;
                    gpuConfig.sizeStartMult = mod.startMultiplier;
                    gpuConfig.sizeEndMult = mod.endMultiplier;
                }
                else if constexpr (std::is_same_v<T, ::vfx::SpeedOverLifetimeConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ModifierFlags::SpeedOverLifetime;
                    gpuConfig.speedStartMult = mod.startMultiplier;
                    gpuConfig.speedEndMult = mod.endMultiplier;
                }
                else if constexpr (std::is_same_v<T, ::vfx::RotationOverLifetimeConfig>)
                {
                    gpuConfig.modifierFlags |= render::vfx::ModifierFlags::RotationOverLifetime;
                    gpuConfig.angularVelocity = glm::radians(mod.angularVelocity);
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
            // No flag set for Point (default behavior)
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

        return gpuConfig;
    }

    render::vfx::GPUEmitterState VFXSceneRenderer::toGPUState(
        const VFXRuntimeInstance& instance) const
    {
        render::vfx::GPUEmitterState gpuState{};
        gpuState.worldTransform = instance.worldTransform;
        gpuState.particleOffset = instance.gpuParticleOffset;
        gpuState.maxParticles = instance.gpuParticleCount;
        gpuState.activeCount = 0;  // Reset by compute shader
        gpuState.spawnThisFrame = 0;  // Set by caller
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

        gpuComputePipeline->insertBarriersBeforeCompute(
            cmd,
            gpuBufferManager->getStateBuffer(),
            gpuBufferManager->getDrawCommandBuffer(),
            gpuBufferManager->getParticleBuffer()
        );

        for (const auto& [id, instance] : instances)
        {
            if (!instance.gpuDriven || !instance.active)
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

        gpuComputePipeline->insertBarriersAfterCompute(
            cmd,
            gpuBufferManager->getParticleBuffer(),
            gpuBufferManager->getStateBuffer(),
            gpuBufferManager->getDrawCommandBuffer()
        );
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
    }
}
