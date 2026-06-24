#include "PrefabRigPreviewAdapter.hpp"

namespace core
{
    PrefabRigPreviewAdapter::~PrefabRigPreviewAdapter() noexcept
    {
        controllers.clear();
    }

    controllers::PrefabRigPreviewController*
    PrefabRigPreviewAdapter::getController(services::PreviewInstanceId instanceId) const
    {
        auto it = controllers.find(instanceId);
        return (it != controllers.end()) ? it->second.get() : nullptr;
    }

    ::controllers::PrefabRigDesc PrefabRigPreviewAdapter::toDesc(const services::PrefabRigDescDTO& dto)
    {
        ::controllers::PrefabRigDesc desc;
        desc.parts.reserve(dto.parts.size());
        for (const auto& p : dto.parts)
        {
            ::controllers::PrefabRigPart part;
            part.meshPath = p.meshPath;
            part.animatorPath = p.animatorPath;
            part.retargetPath = p.retargetPath;
            part.parentPartIndex = p.parentPartIndex;
            part.attachParentSocket = p.attachParentSocket;
            part.attachChildRotation = p.attachChildRotation;
            part.attachChildScale = p.attachChildScale;
            part.localTransform = p.localTransform;
            part.defaultMaterialPath = p.defaultMaterialPath;
            part.subMeshMaterials = p.subMeshMaterials;
            desc.parts.push_back(std::move(part));
        }

        desc.ik.reserve(dto.ik.size());
        for (const auto& k : dto.ik)
        {
            ::controllers::PrefabRigIK chain;
            chain.chain = k.chain;
            chain.bodyPartIndex = k.bodyPartIndex;
            chain.targetPartIndex = k.targetPartIndex;
            chain.targetSocketName = k.targetSocketName;
            desc.ik.push_back(std::move(chain));
        }

        return desc;
    }

    void PrefabRigPreviewAdapter::initPrefabRigPreview(services::PreviewInstanceId instanceId)
    {
        auto& controller = controllers[instanceId];
        if (!controller)
        {
            controller = std::make_unique<::controllers::PrefabRigPreviewController>();
        }
        controller->init();
    }

    bool PrefabRigPreviewAdapter::buildPrefabRigPreview(services::PreviewInstanceId instanceId,
                                                        const services::PrefabRigDescDTO& desc)
    {
        auto* controller = getController(instanceId);
        if (!controller) return false;
        return controller->buildFromDesc(toDesc(desc));
    }

    void PrefabRigPreviewAdapter::cleanUpPrefabRigPreview(services::PreviewInstanceId instanceId)
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

    bool PrefabRigPreviewAdapter::isPrefabRigPreviewBuilt(services::PreviewInstanceId instanceId) const
    {
        auto* controller = getController(instanceId);
        return controller && controller->isBuilt();
    }

    size_t PrefabRigPreviewAdapter::getPrefabRigPartCount(services::PreviewInstanceId instanceId) const
    {
        auto* controller = getController(instanceId);
        return controller ? controller->partCount() : 0;
    }

    void PrefabRigPreviewAdapter::updatePrefabRigPreview(services::PreviewInstanceId instanceId, float deltaTime)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->update(deltaTime);
        }
    }

    void PrefabRigPreviewAdapter::updatePrefabRigCamera(services::PreviewInstanceId instanceId,
                                                        const glm::mat4& view, const glm::mat4& projection,
                                                        const glm::vec3& cameraPos)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->updateCamera(view, projection, cameraPos);
        }
    }

    void PrefabRigPreviewAdapter::setPrefabRigEnvironment(services::PreviewInstanceId instanceId,
                                                          const services::PreviewEnvironmentParams& params)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->setEnvironment(params);
        }
    }

    void PrefabRigPreviewAdapter::setPrefabRigRootMatrix(services::PreviewInstanceId instanceId,
                                                         const glm::mat4& model)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->setRootModelMatrix(model);
        }
    }

    void* PrefabRigPreviewAdapter::renderPrefabRigPreview(services::PreviewInstanceId instanceId)
    {
        auto* controller = getController(instanceId);
        return controller ? controller->render() : nullptr;
    }

    void PrefabRigPreviewAdapter::setPrefabRigState(services::PreviewInstanceId instanceId, size_t part,
                                                    const std::string& stateName, float blendDuration)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->forceState(part, stateName, blendDuration);
        }
    }

    std::vector<services::PrefabRigStateInfo> PrefabRigPreviewAdapter::getPrefabRigStates(
        services::PreviewInstanceId instanceId, size_t part) const
    {
        std::vector<services::PrefabRigStateInfo> result;
        auto* controller = getController(instanceId);
        if (!controller) return result;

        const animator::AnimatorData* data = controller->animatorData(part);
        if (!data) return result;

        result.reserve(data->graph.states.size());
        for (const auto& state : data->graph.states)
        {
            result.push_back(services::PrefabRigStateInfo{state.name});
        }
        return result;
    }

    void PrefabRigPreviewAdapter::setPrefabRigBool(services::PreviewInstanceId instanceId, size_t part,
                                                   const std::string& name, bool value)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->setBool(part, name, value);
        }
    }

    void PrefabRigPreviewAdapter::setPrefabRigFloat(services::PreviewInstanceId instanceId, size_t part,
                                                    const std::string& name, float value)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->setFloat(part, name, value);
        }
    }

    void PrefabRigPreviewAdapter::setPrefabRigInt(services::PreviewInstanceId instanceId, size_t part,
                                                  const std::string& name, int32_t value)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->setInt(part, name, value);
        }
    }

    void PrefabRigPreviewAdapter::setPrefabRigTrigger(services::PreviewInstanceId instanceId, size_t part,
                                                      const std::string& name)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->setTrigger(part, name);
        }
    }

    void PrefabRigPreviewAdapter::playPrefabRig(services::PreviewInstanceId instanceId)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->play();
        }
    }

    void PrefabRigPreviewAdapter::pausePrefabRig(services::PreviewInstanceId instanceId)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->pause();
        }
    }

    bool PrefabRigPreviewAdapter::isPrefabRigPaused(services::PreviewInstanceId instanceId) const
    {
        auto* controller = getController(instanceId);
        return controller && controller->isPaused();
    }

    void PrefabRigPreviewAdapter::stepPrefabRigFrame(services::PreviewInstanceId instanceId, size_t part,
                                                     int frames)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->stepFrame(part, frames);
        }
    }

    void PrefabRigPreviewAdapter::setPrefabRigNormalizedTime(services::PreviewInstanceId instanceId,
                                                             size_t part, float t)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->setNormalizedTime(part, t);
        }
    }

    float PrefabRigPreviewAdapter::getPrefabRigNormalizedTime(services::PreviewInstanceId instanceId,
                                                              size_t part) const
    {
        auto* controller = getController(instanceId);
        return controller ? controller->normalizedTime(part) : 0.0f;
    }

    void PrefabRigPreviewAdapter::setPrefabRigPartPreviewTransform(services::PreviewInstanceId instanceId,
                                                                   size_t part, const glm::mat4& transform)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->setPartPreviewTransform(part, transform);
        }
    }

    void PrefabRigPreviewAdapter::resetPrefabRigPreviewTransforms(services::PreviewInstanceId instanceId)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->resetPreviewTransforms();
        }
    }

    glm::mat4 PrefabRigPreviewAdapter::getPrefabRigPartWorld(services::PreviewInstanceId instanceId,
                                                             size_t part) const
    {
        auto* controller = getController(instanceId);
        return controller ? controller->partWorld(part) : glm::mat4(1.0f);
    }

    std::vector<services::PrefabRigJoint> PrefabRigPreviewAdapter::getPrefabRigJointWorlds(
        services::PreviewInstanceId instanceId, size_t part) const
    {
        auto* controller = getController(instanceId);
        if (!controller) return {};
        return controller->jointWorlds(part);
    }

    std::vector<animator::SocketDefinition> PrefabRigPreviewAdapter::getPrefabRigSockets(
        services::PreviewInstanceId instanceId, size_t part) const
    {
        auto* controller = getController(instanceId);
        if (!controller) return {};
        return controller->editableSockets(part);
    }

    void PrefabRigPreviewAdapter::setPrefabRigSockets(services::PreviewInstanceId instanceId, size_t part,
                                                      const std::vector<animator::SocketDefinition>& sockets)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->editableSockets(part) = sockets;
        }
    }

    std::vector<animator::ik::IKChainConfig> PrefabRigPreviewAdapter::getPrefabRigChains(
        services::PreviewInstanceId instanceId) const
    {
        auto* controller = getController(instanceId);
        if (!controller) return {};
        return controller->editableChains();
    }

    void PrefabRigPreviewAdapter::setPrefabRigChains(services::PreviewInstanceId instanceId,
                                                     const std::vector<animator::ik::IKChainConfig>& chains)
    {
        auto* controller = getController(instanceId);
        if (controller)
        {
            controller->editableChains() = chains;
        }
    }
}
