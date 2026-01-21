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

        cpuPipeline = std::make_unique<render::vfx::VFXScenePipeline>(device, swapChain);
        cpuPipeline->init(sceneRenderPass);

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

        auto configOpt = vfx::VFXEmitterConfigLoader::loadFromFile(params.vfxAssetPath);
        if (configOpt.has_value())
        {
            instance.config = configOpt.value();
        }
        else
        {
            loggerWarning("Failed to load VFX asset: {}, using default config", params.vfxAssetPath);
        }

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
            if (it->second.gpuDriven && gpuBufferManager)
            {
                pendingEmitterFrees.emplace_back(it->second.gpuEmitterIndex, frameNumber);
            }

            loggerInfo("Destroyed VFX instance {}", id);
            instances.erase(it);
        }
    }

    void VFXSceneRenderer::destroyAllInstances()
    {
        device.getLogicalDevice().waitIdle();

        pendingEmitterFrees.clear();

        for (auto& [id, instance] : instances)
        {
            if (instance.gpuDriven && gpuBufferManager)
            {
                gpuBufferManager->freeEmitter(instance.gpuEmitterIndex);
            }
        }
        instances.clear();

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
                gpuBufferManager->freeEmitter(emitterIndex);
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

            uint32_t spawnThisFrame = 0;
            if (instance.active)
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
        gpuState.spawnCounter = 0;
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
