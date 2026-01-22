#include "VFXRuntimeAdapter.hpp"
#include "VFXSceneRenderer.hpp"
#include "../../graphics/core/VulkanContext.hpp"
#include "print/Logger.hpp"

namespace core
{
    VFXRuntimeAdapter::VFXRuntimeAdapter() = default;

    VFXRuntimeAdapter::~VFXRuntimeAdapter() noexcept
    {
        if (renderer)
        {
            renderer->cleanUp();
            renderer.reset();
        }
    }

    void VFXRuntimeAdapter::init(vk::RenderPass sceneRenderPass)
    {
        if (!renderer)
        {
            auto& device = *VulkanContext::getDevice();
            auto& swapChain = *VulkanContext::getSwapChain();
            renderer = std::make_unique<controllers::VFXSceneRenderer>(device, swapChain);
        }
        renderer->init(sceneRenderPass);
    }

    void VFXRuntimeAdapter::cleanUp()
    {
        if (renderer)
        {
            renderer->cleanUp();
        }
    }

    void VFXRuntimeAdapter::recreate(vk::RenderPass sceneRenderPass)
    {
        if (renderer)
        {
            renderer->recreate(sceneRenderPass);
        }
    }

    bool VFXRuntimeAdapter::isInitialized() const
    {
        return renderer && renderer->isInitialized();
    }

    services::VFXInstanceId VFXRuntimeAdapter::createInstance(const services::VFXRuntimeParams& params)
    {
        if (!renderer)
        {
            loggerWarning("VFXRuntimeAdapter::createInstance called before init");
            return 0;
        }

        controllers::VFXRuntimeParams controllerParams;
        controllerParams.vfxAssetPath = params.vfxAssetPath;
        controllerParams.worldTransform = params.worldTransform;
        controllerParams.loop = params.loop;

        return renderer->createInstance(controllerParams);
    }

    void VFXRuntimeAdapter::destroyInstance(services::VFXInstanceId id)
    {
        if (renderer)
        {
            renderer->destroyInstance(id);
        }
    }

    void VFXRuntimeAdapter::setInstanceTransform(services::VFXInstanceId id, const glm::mat4& worldTransform)
    {
        if (renderer)
        {
            renderer->setInstanceTransform(id, worldTransform);
        }
    }

    void VFXRuntimeAdapter::playInstance(services::VFXInstanceId id)
    {
        if (renderer)
        {
            renderer->playInstance(id);
        }
    }

    void VFXRuntimeAdapter::stopInstance(services::VFXInstanceId id)
    {
        if (renderer)
        {
            renderer->stopInstance(id);
        }
    }

    void VFXRuntimeAdapter::resetInstance(services::VFXInstanceId id)
    {
        if (renderer)
        {
            renderer->resetInstance(id);
        }
    }

    bool VFXRuntimeAdapter::isInstancePlaying(services::VFXInstanceId id) const
    {
        return renderer ? renderer->isInstancePlaying(id) : false;
    }

    void VFXRuntimeAdapter::update(float deltaTime)
    {
        if (renderer)
        {
            renderer->update(deltaTime);
        }
    }

    void VFXRuntimeAdapter::setCamera(const glm::mat4& view, const glm::mat4& projection,
                                       const glm::vec3& cameraPos, float time)
    {
        if (renderer)
        {
            renderer->setCamera(view, projection, cameraPos, time);
        }
    }

    void VFXRuntimeAdapter::recordComputeCommands(const vk::CommandBuffer& cmd)
    {
        if (renderer)
        {
            renderer->recordComputeCommands(cmd);
        }
    }

    void VFXRuntimeAdapter::recordDrawCommands(const vk::CommandBuffer& cmd)
    {
        if (renderer)
        {
            renderer->recordDrawCommands(cmd);
        }
    }

    size_t VFXRuntimeAdapter::getInstanceCount() const
    {
        return renderer ? renderer->getInstanceCount() : 0;
    }
}
