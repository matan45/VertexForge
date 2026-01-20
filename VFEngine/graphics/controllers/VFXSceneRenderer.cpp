#include "VFXSceneRenderer.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../render/vfx/VFXScenePipeline.hpp"
#include "../render/vfx/VFXParticleSystem.hpp"
#include "../render/vfx/GPUVFXBufferManager.hpp"
#include "../render/vfx/GPUVFXComputePipeline.hpp"
#include "../render/vfx/VFXSceneGPUPipeline.hpp"
#include "vfx/VFXEmitterConfigLoader.hpp"
#include "print/Logger.hpp"
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

    void VFXSceneRenderer::init(vk::RenderPass sceneRenderPass)
    {
        if (initialized)
        {
            return;
        }

        // Always initialize CPU pipeline as fallback
        cpuPipeline = std::make_unique<render::vfx::VFXScenePipeline>(device, swapChain);
        cpuPipeline->init(sceneRenderPass);

        // Try to initialize GPU mode
        if (gpuDrivenEnabled)
        {
            if (!initGPUMode(sceneRenderPass))
            {
                loggerWarning("GPU-driven VFX initialization failed, falling back to CPU mode");
                gpuDrivenEnabled = false;
            }
        }

        initialized = true;
        loggerInfo("VFX Scene Renderer initialized (GPU mode: {})", gpuDrivenEnabled ? "enabled" : "disabled");
    }

    bool VFXSceneRenderer::initGPUMode(vk::RenderPass renderPass)
    {
        try
        {
            // Create buffer manager
            gpuBufferManager = std::make_unique<render::vfx::GPUVFXBufferManager>(device);
            if (!gpuBufferManager->init())
            {
                loggerError("Failed to initialize GPU VFX buffer manager");
                return false;
            }

            // Create compute pipeline
            gpuComputePipeline = std::make_unique<render::vfx::GPUVFXComputePipeline>(device);
            gpuComputePipeline->init();
            if (!gpuComputePipeline->isInitialized())
            {
                loggerError("Failed to initialize GPU VFX compute pipeline");
                return false;
            }

            // Create render pipeline
            gpuRenderPipeline = std::make_unique<render::vfx::VFXSceneGPUPipeline>(device, swapChain);
            gpuRenderPipeline->init(renderPass);
            if (!gpuRenderPipeline->isInitialized())
            {
                loggerError("Failed to initialize GPU VFX render pipeline");
                return false;
            }

            // Update compute pipeline descriptors
            gpuComputePipeline->updateDescriptors(
                gpuBufferManager->getParticleBuffer(),
                gpuBufferManager->getConfigBuffer(),
                gpuBufferManager->getStateBuffer(),
                gpuBufferManager->getDrawCommandBuffer()
            );

            // Update render pipeline particle buffer
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
    }

    void VFXSceneRenderer::cleanUp()
    {
        if (!initialized)
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        // Destroy all instances
        destroyAllInstances();

        cleanupGPUMode();

        if (cpuPipeline)
        {
            cpuPipeline->cleanUp();
            cpuPipeline.reset();
        }

        initialized = false;
        loggerInfo("VFX Scene Renderer cleaned up");
    }

    void VFXSceneRenderer::setGPUDrivenEnabled(bool enabled)
    {
        if (enabled == gpuDrivenEnabled)
        {
            return;
        }

        if (enabled && !gpuBufferManager)
        {
            loggerWarning("Cannot enable GPU mode - GPU resources not initialized");
            return;
        }

        gpuDrivenEnabled = enabled;
        loggerInfo("VFX GPU mode: {}", enabled ? "enabled" : "disabled");
    }

    VFXInstanceId VFXSceneRenderer::createInstance(const VFXRuntimeParams& params)
    {
        VFXInstanceId id = nextInstanceId++;

        VFXRuntimeInstance instance;
        instance.id = id;
        instance.worldTransform = params.worldTransform;
        instance.loop = params.loop;

        // Load emitter config from asset file
        auto configOpt = vfx::VFXEmitterConfigLoader::loadFromFile(params.vfxAssetPath);
        if (configOpt.has_value())
        {
            instance.config = configOpt.value();
        }
        else
        {
            loggerWarning("Failed to load VFX asset: {}, using default config", params.vfxAssetPath);
        }

        // Try GPU allocation first
        if (gpuDrivenEnabled && gpuBufferManager)
        {
            auto allocation = gpuBufferManager->allocateEmitter(
                render::vfx::GPUVFXConstants::DEFAULT_PARTICLES_PER_EMITTER);

            if (allocation.emitterIndex != UINT32_MAX)
            {
                instance.gpuDriven = true;
                instance.gpuEmitterIndex = allocation.emitterIndex;
                instance.gpuParticleOffset = allocation.particleOffset;
                instance.gpuParticleCount = allocation.particleCount;
                instance.particleSystem = nullptr;  // No CPU particle system needed
                instance.active = true;

                loggerInfo("Created GPU-driven VFX instance {} with {} particles at offset {}",
                           id, instance.gpuParticleCount, instance.gpuParticleOffset);
            }
            else
            {
                loggerWarning("GPU allocation failed for VFX instance {}, using CPU fallback", id);
            }
        }

        // Fall back to CPU mode
        if (!instance.gpuDriven)
        {
            instance.particleSystem = std::make_unique<render::vfx::VFXParticleSystem>();
            instance.particleSystem->setEmitterConfig(instance.config);
            instance.active = true;
        }

        instances[id] = std::move(instance);

        loggerInfo("Created VFX instance {} from asset: {} (GPU: {})",
                   id, params.vfxAssetPath, instances[id].gpuDriven);
        return id;
    }

    void VFXSceneRenderer::destroyInstance(VFXInstanceId id)
    {
        auto it = instances.find(id);
        if (it != instances.end())
        {
            // Wait for GPU to finish using the buffers
            if (it->second.gpuDriven)
            {
                device.getLogicalDevice().waitIdle();
            }

            // Free GPU resources if allocated
            if (it->second.gpuDriven && gpuBufferManager)
            {
                gpuBufferManager->freeEmitter(it->second.gpuEmitterIndex);
            }

            loggerInfo("Destroyed VFX instance {}", id);
            instances.erase(it);
        }
    }

    void VFXSceneRenderer::destroyAllInstances()
    {
        // Wait for GPU to finish using the buffers
        device.getLogicalDevice().waitIdle();

        for (auto& [id, instance] : instances)
        {
            if (instance.gpuDriven && gpuBufferManager)
            {
                gpuBufferManager->freeEmitter(instance.gpuEmitterIndex);
            }
        }
        instances.clear();

        // Reset particle buffer cleared flag so it gets cleared again on next use
        if (gpuBufferManager)
        {
            gpuBufferManager->resetParticleBufferClearedFlag();
        }

        loggerInfo("Destroyed all VFX instances");
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

    void VFXSceneRenderer::update(float deltaTime)
    {
        if (gpuDrivenEnabled)
        {
            updateGPU(deltaTime);
        }
        else
        {
            updateCPU(deltaTime);
        }

        frameNumber++;
    }

    void VFXSceneRenderer::updateCPU(float deltaTime)
    {
        for (auto& [id, instance] : instances)
        {
            if (instance.gpuDriven)
            {
                continue;  // Skip GPU instances in CPU update
            }

            if (instance.particleSystem && instance.active)
            {
                instance.particleSystem->update(deltaTime);

                // Check if non-looping VFX has finished
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

    void VFXSceneRenderer::updateGPU(float deltaTime)
    {
        if (!gpuBufferManager)
        {
            return;
        }

        // Update CPU instances (fallback)
        for (auto& [id, instance] : instances)
        {
            if (!instance.gpuDriven && instance.particleSystem && instance.active)
            {
                instance.particleSystem->update(deltaTime);
            }
        }

        // Update GPU emitter configs and states
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dist;

        for (auto& [id, instance] : instances)
        {
            if (!instance.gpuDriven || !instance.active)
            {
                continue;
            }

            // Calculate spawn count for this frame
            uint32_t spawnThisFrame = 0;
            if (instance.active)
            {
                instance.spawnAccumulator += instance.config.spawnRate * deltaTime;
                spawnThisFrame = static_cast<uint32_t>(instance.spawnAccumulator);
                instance.spawnAccumulator -= static_cast<float>(spawnThisFrame);
            }

            // Create GPU config
            auto gpuConfig = toGPUConfig(
                instance.config,
                deltaTime,
                instance.gpuParticleCount,
                dist(gen)
            );
            gpuBufferManager->updateEmitterConfig(instance.gpuEmitterIndex, gpuConfig);

            // Create GPU state
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
        gpuConfig.padding = 0.0f;
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
        gpuState.completedWorkgroups = 0;
        gpuState.padding = 0;
        return gpuState;
    }

    void VFXSceneRenderer::setCamera(const glm::mat4& view, const glm::mat4& projection,
                                      const glm::vec3& cameraPos, float time)
    {
        currentView = view;
        currentProjection = projection;
        currentCameraPos = cameraPos;
        currentTime = time;

        if (cpuPipeline && cpuPipeline->isInitialized())
        {
            cpuPipeline->updateCameraUBO(view, projection, cameraPos, time);
        }

        if (gpuRenderPipeline && gpuRenderPipeline->isInitialized())
        {
            gpuRenderPipeline->updateCameraUBO(view, projection, cameraPos, time);
        }
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

        // Count active GPU emitters
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

        // Clear particle buffer on first use (sets all flags to inactive)
        gpuBufferManager->clearParticleBufferIfNeeded(cmd);

        // Upload state buffer from staging to device-local
        gpuBufferManager->uploadStateBuffer(cmd);

        // Clear draw commands (sets instanceCount=0 for all emitters)
        gpuBufferManager->clearDrawCommands(cmd);

        // Reset all active counts before compute
        gpuBufferManager->resetAllActiveCounts(cmd);

        // Insert barrier: Transfer → Compute
        gpuComputePipeline->insertBarriersBeforeCompute(
            cmd,
            gpuBufferManager->getStateBuffer(),
            gpuBufferManager->getDrawCommandBuffer(),
            gpuBufferManager->getParticleBuffer()
        );

        // Dispatch compute for each active GPU emitter
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

        // Insert barrier: Compute → Indirect Draw + Vertex Shader
        gpuComputePipeline->insertBarriersAfterCompute(
            cmd,
            gpuBufferManager->getParticleBuffer(),
            gpuBufferManager->getStateBuffer(),
            gpuBufferManager->getDrawCommandBuffer()
        );
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

        // Always record CPU instances (fallback or mixed mode)
        recordCPUDrawCommands(cmd);
    }

    void VFXSceneRenderer::recordCPUDrawCommands(vk::CommandBuffer cmd)
    {
        if (!cpuPipeline || !cpuPipeline->isInitialized())
        {
            return;
        }

        // Collect CPU particle instances
        collectAllParticleInstances();

        if (collectedInstances.empty())
        {
            return;
        }

        cpuPipeline->setParticleInstances(collectedInstances);
        cpuPipeline->recordCommandsInline(cmd);
    }

    void VFXSceneRenderer::recordGPUDrawCommands(vk::CommandBuffer cmd)
    {
        if (!gpuRenderPipeline || !gpuRenderPipeline->isInitialized() || !gpuBufferManager)
        {
            return;
        }

        // Count active GPU emitters
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

    void VFXSceneRenderer::collectAllParticleInstances()
    {
        collectedInstances.clear();

        for (const auto& [id, instance] : instances)
        {
            // Skip GPU-driven instances (they use indirect draw)
            if (instance.gpuDriven)
            {
                continue;
            }

            if (!instance.particleSystem || !instance.active)
            {
                continue;
            }

            auto particleData = instance.particleSystem->getInstanceData();

            // Transform particle positions by the instance's world transform
            for (auto& particle : particleData)
            {
                glm::vec4 worldPos = instance.worldTransform * glm::vec4(particle.worldPosition, 1.0f);
                particle.worldPosition = glm::vec3(worldPos);
            }

            collectedInstances.insert(collectedInstances.end(), particleData.begin(), particleData.end());
        }
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
                // For GPU instances, we'd need to read back from GPU
                // For now, estimate based on allocation
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
