#include "VFXPreviewAdapter.hpp"
#include "print/Logger.hpp"

namespace core
{
    VFXPreviewAdapter::~VFXPreviewAdapter() noexcept
    {
        controllers.clear();
    }

    controllers::VFXPreviewController* VFXPreviewAdapter::getController(
        services::PreviewInstanceId instanceId) const
    {
        auto it = controllers.find(instanceId);
        return (it != controllers.end()) ? it->second.get() : nullptr;
    }

    void VFXPreviewAdapter::initVFXPreview(services::PreviewInstanceId instanceId)
    {
        auto& controller = controllers[instanceId];
        if (!controller)
        {
            controller = std::make_unique<::controllers::VFXPreviewController>();
        }
        controller->init();
    }

    void VFXPreviewAdapter::cleanUpVFXPreview(services::PreviewInstanceId instanceId)
    {
        auto it = controllers.find(instanceId);
        if (it != controllers.end())
        {
            if (it->second)
            {
                it->second->cleanUp();
            }
            controllers.erase(it);
        }
    }

    bool VFXPreviewAdapter::isVFXPreviewInitialized(services::PreviewInstanceId instanceId) const
    {
        auto* controller = getController(instanceId);
        return controller && controller->isInitialized();
    }

    void VFXPreviewAdapter::setVFXParams(services::PreviewInstanceId instanceId,
                                          const services::VFXPreviewParams& params)
    {
        auto* controller = getController(instanceId);
        if (!controller) return;

        controllers::VFXPreviewParams controllerParams;
        controllerParams.spawnRate = params.spawnRate;
        controllerParams.lifetime = params.lifetime;
        controllerParams.startSize = params.startSize;
        controllerParams.startSpeed = params.startSpeed;
        controllerParams.startColor = params.startColor;
        controllerParams.emitDirection = params.emitDirection;
        controllerParams.texturePath = params.texturePath;

        controller->setParams(controllerParams);
    }

    services::VFXPreviewParams VFXPreviewAdapter::getVFXParams(services::PreviewInstanceId instanceId) const
    {
        services::VFXPreviewParams result;
        auto* controller = getController(instanceId);
        if (!controller) return result;

        const auto& controllerParams = controller->getParams();
        result.spawnRate = controllerParams.spawnRate;
        result.lifetime = controllerParams.lifetime;
        result.startSize = controllerParams.startSize;
        result.startSpeed = controllerParams.startSpeed;
        result.startColor = controllerParams.startColor;
        result.emitDirection = controllerParams.emitDirection;
        result.texturePath = controllerParams.texturePath;
        return result;
    }

    void VFXPreviewAdapter::updateVFXCamera(services::PreviewInstanceId instanceId, const glm::mat4& view,
                                             const glm::mat4& projection, const glm::vec3& cameraPos, float time)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->updateCamera(view, projection, cameraPos, time);
        }
    }

    void VFXPreviewAdapter::updateVFXSimulation(services::PreviewInstanceId instanceId, float deltaTime)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->update(deltaTime);
        }
    }

    void VFXPreviewAdapter::playVFX(services::PreviewInstanceId instanceId)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->play();
        }
    }

    void VFXPreviewAdapter::pauseVFX(services::PreviewInstanceId instanceId)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->pause();
        }
    }

    void VFXPreviewAdapter::stopVFX(services::PreviewInstanceId instanceId)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->stop();
        }
    }

    bool VFXPreviewAdapter::isVFXPlaying(services::PreviewInstanceId instanceId) const
    {
        auto* controller = getController(instanceId);
        return controller ? controller->isPlaying() : false;
    }

    void* VFXPreviewAdapter::renderVFXPreview(services::PreviewInstanceId instanceId)
    {
        auto* controller = getController(instanceId);
        return controller ? controller->render() : nullptr;
    }
}
