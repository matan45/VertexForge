#include "IKComponentService.hpp"
#include "../../providers/physics/IIKProvider.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/physics/IKEvents.hpp"

namespace services
{
    IKComponentService::IKComponentService(IIKProvider* provider)
        : ikProvider(provider)
    {
    }

    bool IKComponentService::addIKComponent(EntityHandle entity)
    {
        return ikProvider->addIKComponent(entity);
    }

    bool IKComponentService::removeIKComponent(EntityHandle entity)
    {
        return ikProvider->removeIKComponent(entity);
    }

    bool IKComponentService::hasIKComponent(EntityHandle entity) const
    {
        return ikProvider->hasIKComponent(entity);
    }

    bool IKComponentService::addChain(EntityHandle entity, const std::string& chainName,
                                       const std::string& tipBoneName,
                                       const std::vector<std::string>& chainBoneNames)
    {
        return ikProvider->addChain(entity, chainName, tipBoneName, chainBoneNames);
    }

    bool IKComponentService::removeChain(EntityHandle entity, const std::string& chainName)
    {
        return ikProvider->removeChain(entity, chainName);
    }

    void IKComponentService::updateChainConfig(EntityHandle entity, const std::string& chainName,
                                                const std::string& tipBoneName,
                                                const std::vector<std::string>& chainBoneNames,
                                                const std::vector<animator::ik::JointConstraint>& constraints,
                                                float weight, bool enabled)
    {
        ikProvider->updateChainConfig(entity, chainName, tipBoneName,
                                       chainBoneNames, constraints, weight, enabled);
    }

    void IKComponentService::setTarget(EntityHandle entity, const std::string& chainName,
                                        const glm::vec3& position,
                                        const std::optional<glm::quat>& rotation)
    {
        ikProvider->setTarget(entity, chainName, position, rotation);
    }

    void IKComponentService::setChainWeight(EntityHandle entity, const std::string& chainName,
                                             float weight)
    {
        ikProvider->setChainWeight(entity, chainName, weight);
    }

    void IKComponentService::setChainEnabled(EntityHandle entity, const std::string& chainName,
                                              bool enabled)
    {
        ikProvider->setChainEnabled(entity, chainName, enabled);
    }

    std::vector<std::string> IKComponentService::getChainNames(EntityHandle entity) const
    {
        return ikProvider->getChainNames(entity);
    }

    float IKComponentService::getChainWeight(EntityHandle entity,
                                              const std::string& chainName) const
    {
        return ikProvider->getChainWeight(entity, chainName);
    }

    bool IKComponentService::isChainEnabled(EntityHandle entity,
                                             const std::string& chainName) const
    {
        return ikProvider->isChainEnabled(entity, chainName);
    }

    void IKComponentService::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::ik::SetIKTargetCommand>(
            [this](const events::ik::SetIKTargetCommand& cmd) {
                setTarget(cmd.entity, cmd.chainName, cmd.targetPosition, cmd.targetRotation);
            });

        dispatcher.registerCommandHandler<events::ik::SetIKChainWeightCommand>(
            [this](const events::ik::SetIKChainWeightCommand& cmd) {
                setChainWeight(cmd.entity, cmd.chainName, cmd.weight);
            });

        dispatcher.registerCommandHandler<events::ik::SetIKChainEnabledCommand>(
            [this](const events::ik::SetIKChainEnabledCommand& cmd) {
                setChainEnabled(cmd.entity, cmd.chainName, cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::ik::AddIKComponentCommand>(
            [this](const events::ik::AddIKComponentCommand& cmd) -> bool {
                return addIKComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ik::RemoveIKComponentCommand>(
            [this](const events::ik::RemoveIKComponentCommand& cmd) -> bool {
                return removeIKComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ik::AddIKChainCommand>(
            [this](const events::ik::AddIKChainCommand& cmd) -> bool {
                return addChain(cmd.entity, cmd.chainName, cmd.tipBoneName, cmd.chainBoneNames);
            });

        dispatcher.registerCommandHandler<events::ik::RemoveIKChainCommand>(
            [this](const events::ik::RemoveIKChainCommand& cmd) -> bool {
                return removeChain(cmd.entity, cmd.chainName);
            });

        dispatcher.registerCommandHandler<events::ik::UpdateIKChainConfigCommand>(
            [this](const events::ik::UpdateIKChainConfigCommand& cmd) {
                updateChainConfig(cmd.entity, cmd.chainName, cmd.tipBoneName,
                                   cmd.chainBoneNames, cmd.constraints,
                                   cmd.weight, cmd.enabled);
            });

        dispatcher.registerQueryHandler<events::ik::GetIKChainNamesQuery>(
            [this](const events::ik::GetIKChainNamesQuery& q) -> std::vector<std::string> {
                return getChainNames(q.entity);
            });

        dispatcher.registerQueryHandler<events::ik::GetIKChainWeightQuery>(
            [this](const events::ik::GetIKChainWeightQuery& q) -> float {
                return getChainWeight(q.entity, q.chainName);
            });

        dispatcher.registerQueryHandler<events::ik::IsIKChainEnabledQuery>(
            [this](const events::ik::IsIKChainEnabledQuery& q) -> bool {
                return isChainEnabled(q.entity, q.chainName);
            });

        dispatcher.registerQueryHandler<events::ik::HasIKComponentQuery>(
            [this](const events::ik::HasIKComponentQuery& q) -> bool {
                return hasIKComponent(q.entity);
            });
    }
}
