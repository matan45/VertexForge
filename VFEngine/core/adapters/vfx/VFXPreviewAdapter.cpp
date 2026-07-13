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

    controllers::VFXPreviewParams VFXPreviewAdapter::toControllerParams(const services::VFXPreviewParams& params)
    {
        controllers::VFXPreviewParams controllerParams;
        controllerParams.spawnRate = params.spawnRate;
        controllerParams.lifetime = params.lifetime;
        controllerParams.startSize = params.startSize;
        controllerParams.startSpeed = params.startSpeed;
        controllerParams.startColor = params.startColor;
        controllerParams.emitDirection = params.emitDirection;
        controllerParams.texturePath = params.texturePath;
        controllerParams.looping = params.looping;
        controllerParams.loopDuration = params.loopDuration;
        controllerParams.sizeVariance = params.sizeVariance;
        controllerParams.lifetimeVariance = params.lifetimeVariance;
        controllerParams.speedVariance = params.speedVariance;
        controllerParams.rotationVariance = params.rotationVariance;
        controllerParams.angularVelocityVariance = params.angularVelocityVariance;
        controllerParams.colorValueVariance = params.colorValueVariance;
        controllerParams.alphaVariance = params.alphaVariance;
        controllerParams.modifiers = params.modifiers;
        controllerParams.forces = params.forces;
        controllerParams.shape = params.shape;
        controllerParams.bursts = params.bursts;
        controllerParams.flipbookRows = params.flipbookRows;
        controllerParams.flipbookColumns = params.flipbookColumns;
        controllerParams.flipbookFrameRate = params.flipbookFrameRate;
        controllerParams.flipbookRandomStart = params.flipbookRandomStart;
        controllerParams.flipbookFrameBlend = params.flipbookFrameBlend;
        controllerParams.alphaClipThreshold = params.alphaClipThreshold;
        controllerParams.blendMode = params.blendMode; // VK-1472
        controllerParams.renderMode = params.renderMode;
        controllerParams.softParticleDistance = params.softParticleDistance;
        controllerParams.stretchMultiplier = params.stretchMultiplier;
        controllerParams.meshPath = params.meshPath;
        controllerParams.materialPath = params.materialPath; // VK-1526
        controllerParams.meshOrientationMode = params.meshOrientationMode; // VK-1476
        controllerParams.meshOrientationAxis = params.meshOrientationAxis;
        controllerParams.meshOrientationSpinRate = params.meshOrientationSpinRate;
        controllerParams.maxTrailPoints = params.maxTrailPoints;
        controllerParams.ribbonWidth = params.ribbonWidth;
        controllerParams.ribbonMinDistance = params.ribbonMinDistance;
        controllerParams.ribbonWidthCurve = params.ribbonWidthCurve;
        controllerParams.ribbonTailGradient = params.ribbonTailGradient;
        controllerParams.hasRibbonWidthCurve = params.hasRibbonWidthCurve;
        controllerParams.hasRibbonTailGradient = params.hasRibbonTailGradient;
        controllerParams.uvScrollSpeedU = params.uvScrollSpeedU;
        controllerParams.uvScrollSpeedV = params.uvScrollSpeedV;
        controllerParams.events = params.events;
        controllerParams.emissiveIntensity = params.emissiveIntensity;
        controllerParams.lightingInfluence = params.lightingInfluence;
        controllerParams.normalMode = params.normalMode;
        controllerParams.ambientAmount = params.ambientAmount;
        controllerParams.collisionEnabled = params.collisionEnabled;
        controllerParams.collisionBounce = params.collisionBounce;
        controllerParams.collisionFriction = params.collisionFriction;
        controllerParams.collisionLifetimeLoss = params.collisionLifetimeLoss;

        return controllerParams;
    }

    void VFXPreviewAdapter::setVFXParams(services::PreviewInstanceId instanceId,
                                         const services::VFXPreviewParams& params)
    {
        auto* controller = getController(instanceId);
        if (!controller) return;

        controller->setParams(toControllerParams(params));
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

    void VFXPreviewAdapter::setVFXSequence(services::PreviewInstanceId instanceId,
                                           const services::VFXSequencePreviewDesc& desc)
    {
        auto* controller = getController(instanceId);
        if (!controller) return;

        controllers::VFXSequencePreviewDesc ctrlDesc;
        ctrlDesc.seed = desc.seed;
        ctrlDesc.playbackRate = desc.playbackRate;
        ctrlDesc.fixedStep = desc.fixedStep;
        ctrlDesc.markers = desc.markers;
        ctrlDesc.steps.reserve(desc.steps.size());
        for (const auto& step : desc.steps)
        {
            controllers::VFXSequencePreviewStep ctrlStep;
            ctrlStep.params = toControllerParams(step.params);
            ctrlStep.localTransform = step.localTransform;
            ctrlStep.seed = step.seed;
            ctrlStep.startTime = step.startTime;
            ctrlStep.duration = step.duration;
            ctrlStep.loop = step.loop;
            ctrlStep.stopMode = step.stopMode;
            ctrlStep.cueName = step.cueName;
            ctrlDesc.steps.push_back(std::move(ctrlStep));
        }

        controller->setSequence(ctrlDesc);
    }

    void VFXPreviewAdapter::seekVFX(services::PreviewInstanceId instanceId, float seconds)
    {
        auto* controller = getController(instanceId);
        if (controller)
            controller->seekSequence(seconds);
    }

    void VFXPreviewAdapter::setVFXRate(services::PreviewInstanceId instanceId, float rate)
    {
        auto* controller = getController(instanceId);
        if (controller)
            controller->setSequenceRate(rate);
    }
}
