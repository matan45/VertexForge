#include "VFXSceneRenderer.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../render/vfx/VFXScenePipeline.hpp"
#include "../render/vfx/VFXParticleSystem.hpp"
#include "../render/vfx/GPUVFXBufferManager.hpp"
#include "../render/vfx/GPUVFXComputePipeline.hpp"
#include "../render/vfx/VFXSceneGPUPipeline.hpp"
#include "../render/vfx/VFXMeshGPUPipeline.hpp"
#include "../render/vfx/VFXRibbonGPUPipeline.hpp"
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

        auto configOpt = vfx::VFXEmitterConfigLoader::loadFromFile(params.vfxAssetPath);
        if (configOpt.has_value())
        {
            instance.config = configOpt.value();
        }
        else
        {
            vfLogWarning("Failed to load VFX asset: {}, using default config", params.vfxAssetPath);
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
                instance.active = false;

                vfLogInfo("Created GPU-driven VFX instance {} with {} particles at offset {}",
                           id, instance.gpuParticleCount, instance.gpuParticleOffset);
            }
            else
            {
                vfLogWarning("GPU allocation failed for VFX instance {}, using CPU fallback", id);
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

        vfLogInfo("Created VFX instance {} from asset: {} (GPU: {})",
                   id, params.vfxAssetPath, instances[id].gpuDriven);
        return id;
    }

    void VFXSceneRenderer::destroyInstance(VFXInstanceId id)
    {
        auto it = instances.find(id);
        if (it != instances.end())
        {
            if (it->second.gpuDriven)
            {
                emitterIndexToInstanceId.erase(it->second.gpuEmitterIndex);

                if (gpuBufferManager)
                    pendingEmitterFrees.emplace_back(it->second.gpuEmitterIndex, frameNumber);
                if (gpuMeshPipeline)
                    gpuMeshPipeline->removeEmitter(it->second.gpuEmitterIndex);
                if (gpuRibbonPipeline)
                    gpuRibbonPipeline->removeEmitter(it->second.gpuEmitterIndex);
            }

            instances.erase(it);

            // Destroy any sub-emitters owned by this instance
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
                if (gpuBufferManager)
                {
                    gpuBufferManager->resetParticleBufferClearedFlag();
                    gpuBufferManager->resetAllocator();
                }
            }
        }
    }

    void VFXSceneRenderer::destroyAllInstances()
    {
        device.getLogicalDevice().waitIdle();

        activeSubEmitters.clear();
        pendingEmitterFrees.clear();
        instances.clear();
        emitterIndexToInstanceId.clear();

        if (gpuBufferManager)
        {
            gpuBufferManager->resetParticleBufferClearedFlag();
            gpuBufferManager->resetAllocator();
        }

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

    void VFXSceneRenderer::setCamera(const services::VFXCameraParams& camera)
    {
        currentView = camera.view;
        currentProjection = camera.projection;
        currentCameraPos = camera.cameraPos;
        currentTime = camera.time;

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
