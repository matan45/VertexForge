#include "VFXSceneRenderer.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../render/vfx/VFXScenePipeline.hpp"
#include "../render/vfx/VFXParticleSystem.hpp"
#include "vfx/VFXEmitterConfigLoader.hpp"
#include "print/Logger.hpp"

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

        pipeline = std::make_unique<render::vfx::VFXScenePipeline>(device, swapChain);
        pipeline->init(sceneRenderPass);

        initialized = true;
        loggerInfo("VFX Scene Renderer initialized");
    }

    void VFXSceneRenderer::recreate(vk::RenderPass sceneRenderPass)
    {
        if (!initialized)
        {
            return;
        }

        if (pipeline)
        {
            pipeline->recreate(sceneRenderPass);
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

        if (pipeline)
        {
            pipeline->cleanUp();
            pipeline.reset();
        }

        initialized = false;
        loggerInfo("VFX Scene Renderer cleaned up");
    }

    VFXInstanceId VFXSceneRenderer::createInstance(const VFXRuntimeParams& params)
    {
        VFXInstanceId id = nextInstanceId++;

        VFXRuntimeInstance instance;
        instance.id = id;
        instance.worldTransform = params.worldTransform;
        instance.loop = params.loop;
        instance.particleSystem = std::make_unique<render::vfx::VFXParticleSystem>();

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

        instance.particleSystem->setEmitterConfig(instance.config);
        instance.active = true;

        instances[id] = std::move(instance);

        loggerInfo("Created VFX instance {} from asset: {}", id, params.vfxAssetPath);
        return id;
    }

    void VFXSceneRenderer::destroyInstance(VFXInstanceId id)
    {
        auto it = instances.find(id);
        if (it != instances.end())
        {
            loggerInfo("Destroyed VFX instance {}", id);
            instances.erase(it);
        }
    }

    void VFXSceneRenderer::destroyAllInstances()
    {
        instances.clear();
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
        if (it != instances.end() && it->second.particleSystem)
        {
            it->second.particleSystem->setPlaying(true);
            it->second.active = true;
        }
    }

    void VFXSceneRenderer::stopInstance(VFXInstanceId id)
    {
        auto it = instances.find(id);
        if (it != instances.end() && it->second.particleSystem)
        {
            it->second.particleSystem->setPlaying(false);
        }
    }

    void VFXSceneRenderer::resetInstance(VFXInstanceId id)
    {
        auto it = instances.find(id);
        if (it != instances.end() && it->second.particleSystem)
        {
            it->second.particleSystem->reset();
            it->second.active = true;
        }
    }

    bool VFXSceneRenderer::isInstancePlaying(VFXInstanceId id) const
    {
        auto it = instances.find(id);
        if (it != instances.end() && it->second.particleSystem)
        {
            return it->second.particleSystem->isPlaying();
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
        for (auto& [id, instance] : instances)
        {
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

    void VFXSceneRenderer::setCamera(const glm::mat4& view, const glm::mat4& projection,
                                      const glm::vec3& cameraPos, float time)
    {
        currentView = view;
        currentProjection = projection;
        currentCameraPos = cameraPos;
        currentTime = time;

        if (pipeline && pipeline->isInitialized())
        {
            pipeline->updateCameraUBO(view, projection, cameraPos, time);
        }
    }

    void VFXSceneRenderer::recordDrawCommands(const vk::CommandBuffer& cmd)
    {
        if (!initialized || !pipeline || !pipeline->isInitialized())
        {
            return;
        }

        // Collect all particle instances from all VFX emitters
        collectAllParticleInstances();

        if (collectedInstances.empty())
        {
            return;
        }

        // Upload to pipeline and draw
        pipeline->setParticleInstances(collectedInstances);
        pipeline->recordCommandsInline(cmd);
    }

    void VFXSceneRenderer::collectAllParticleInstances()
    {
        collectedInstances.clear();

        for (const auto& [id, instance] : instances)
        {
            if (!instance.particleSystem || !instance.active)
            {
                continue;
            }

            auto particleData = instance.particleSystem->getInstanceData();

            // Transform particle positions by the instance's world transform
            for (auto& particle : particleData)
            {
                // Apply world transform to particle position
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
            if (instance.particleSystem && instance.active)
            {
                total += instance.particleSystem->getActiveParticleCount();
            }
        }
        return total;
    }
}
