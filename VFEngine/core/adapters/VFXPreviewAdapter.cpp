#include "VFXPreviewAdapter.hpp"

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
        controllerParams.looping = params.looping;
        controllerParams.modifiers = params.modifiers;
        controllerParams.forces = params.forces;
        controllerParams.shape = params.shape;
        controllerParams.flipbookRows = params.flipbookRows;
        controllerParams.flipbookColumns = params.flipbookColumns;
        controllerParams.flipbookFrameRate = params.flipbookFrameRate;
        controllerParams.flipbookRandomStart = params.flipbookRandomStart;
        controllerParams.alphaClipThreshold = params.alphaClipThreshold;
        controllerParams.additiveBlend = params.additiveBlend;
        controllerParams.renderMode = params.renderMode;
        controllerParams.softParticleDistance = params.softParticleDistance;
        controllerParams.stretchMultiplier = params.stretchMultiplier;
        controllerParams.meshPath = params.meshPath;
        controllerParams.maxTrailPoints = params.maxTrailPoints;
        controllerParams.ribbonWidth = params.ribbonWidth;
        controllerParams.ribbonMinDistance = params.ribbonMinDistance;

        controller->setParams(controllerParams);
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

    void* VFXPreviewAdapter::renderVFXPreview(services::PreviewInstanceId instanceId)
    {
        auto* controller = getController(instanceId);
        return controller ? controller->render() : nullptr;
    }
}
