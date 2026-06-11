#include "PhysicsAnimationServiceImpl.hpp"
#include "../../events/physics/PhysicsAnimationEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace services
{
    PhysicsAnimationServiceImpl::PhysicsAnimationServiceImpl(IPhysicsProvider* physicsProvider)
        : physicsProvider(physicsProvider)
    {
    }

    PhysicsAnimationServiceImpl::~PhysicsAnimationServiceImpl() = default;

    void PhysicsAnimationServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::physicsAnimation::ActivateRagdollCommand>(
            [this](const auto& cmd) {
                activateRagdoll(cmd.entity, cmd.impulse, cmd.impulseAnimBoneIndex);
            });

        dispatcher.registerCommandHandler<events::physicsAnimation::DeactivateRagdollCommand>(
            [this](const auto& cmd) {
                deactivateRagdoll(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::physicsAnimation::SetKinematicBonesEnabledCommand>(
            [this](const auto& cmd) {
                if (!physicsProvider) return;
                if (cmd.enabled)
                {
                    auto& registry = scene::EntityRegistry::getRegistry();
                    auto entity = internal::fromHandle(cmd.entity);
                    glm::vec3 pos{0.0f};
                    if (registry.valid(entity) && registry.all_of<components::TransformComponent>(entity))
                        pos = registry.get<components::TransformComponent>(entity).position;
                    physicsProvider->createKinematicBones(cmd.entity, pos);
                }
                else
                {
                    physicsProvider->destroyKinematicBones(cmd.entity);
                }
            });

        dispatcher.registerCommandHandler<events::physicsAnimation::ApplyRagdollImpulseCommand>(
            [this](const auto& cmd) {
                applyRagdollImpulse(cmd.entity, cmd.impulse);
            });

        dispatcher.registerCommandHandler<events::physicsAnimation::ApplyRagdollBoneImpulseCommand>(
            [this](const auto& cmd) {
                applyRagdollBoneImpulse(cmd.entity, cmd.animBoneIndex, cmd.impulse);
            });

        dispatcher.registerQueryHandler<events::physicsAnimation::IsRagdollActiveQuery>(
            [this](const auto& query) {
                return isRagdollActive(query.entity);
            });

        dispatcher.registerQueryHandler<events::physicsAnimation::GetPhysicsAnimationModeQuery>(
            [this](const auto& query) {
                return getMode(query.entity);
            });

        dispatcher.registerQueryHandler<events::physicsAnimation::HasPhysicsAnimationQuery>(
            [this](const auto& query) {
                return hasPhysicsAnimation(query.entity);
            });

        dispatcher.registerCommandHandler<events::physicsAnimation::SetPhysicsAnimationModeCommand>(
            [this](const auto& cmd) {
                setMode(cmd.entity, cmd.mode);
            });

        dispatcher.registerCommandHandler<events::physicsAnimation::SetBoneMotorStrengthCommand>(
            [this](const auto& cmd) {
                setBoneMotorStrength(cmd.entity, cmd.boneName, cmd.strength);
            });

        dispatcher.registerCommandHandler<events::physicsAnimation::SetGlobalMotorStrengthCommand>(
            [this](const auto& cmd) {
                setGlobalMotorStrength(cmd.entity, cmd.strength);
            });

        dispatcher.registerCommandHandler<events::physicsAnimation::HitReactionCommand>(
            [this](const auto& cmd) {
                applyHitReaction(cmd.entity, cmd.boneName, cmd.impulse, cmd.recoverTime);
            });

        dispatcher.registerQueryHandler<events::physicsAnimation::IsRagdollSettledQuery>(
            [this](const auto& query) {
                return isRagdollSettled(query.entity);
            });
    }

    void PhysicsAnimationServiceImpl::activateRagdoll(EntityHandle entity, const glm::vec3& impulse,
                                                       int impulseAnimBoneIndex)
    {
        if (!physicsProvider || !physicsProvider->hasPhysicsAnimation(entity)) return;

        physicsProvider->activateRagdoll(entity);

        auto enttEntity = internal::fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();
        if (registry.valid(enttEntity) && registry.all_of<components::PhysicsAnimationComponent>(enttEntity))
        {
            auto& physAnimComp = registry.get<components::PhysicsAnimationComponent>(enttEntity);
            physAnimComp.currentMode = types::PhysicsAnimationMode::Ragdoll;
        }

        if (glm::length(impulse) > 0.001f)
        {
            if (impulseAnimBoneIndex >= 0)
                physicsProvider->applyRagdollBoneImpulse(entity, impulseAnimBoneIndex, impulse);
            else
                physicsProvider->applyRagdollImpulse(entity, impulse);
        }

        events::physicsAnimation::RagdollActivatedNotification notification;
        notification.entity = entity;
        ::events::EventDispatcher::instance().publish(notification);
    }

    void PhysicsAnimationServiceImpl::deactivateRagdoll(EntityHandle entity)
    {
        if (!physicsProvider) return;

        physicsProvider->deactivateRagdoll(entity);

        auto enttEntity = internal::fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();
        if (registry.valid(enttEntity) && registry.all_of<components::PhysicsAnimationComponent>(enttEntity))
        {
            auto& physAnimComp = registry.get<components::PhysicsAnimationComponent>(enttEntity);
            physAnimComp.currentMode = types::PhysicsAnimationMode::Animated;
            physAnimComp.overrideBoneMatrices.clear();
        }

        events::physicsAnimation::RagdollDeactivatedNotification notification;
        notification.entity = entity;
        ::events::EventDispatcher::instance().publish(notification);
    }

    bool PhysicsAnimationServiceImpl::isRagdollActive(EntityHandle entity) const
    {
        return physicsProvider && physicsProvider->isRagdollActive(entity);
    }

    types::PhysicsAnimationMode PhysicsAnimationServiceImpl::getMode(EntityHandle entity) const
    {
        auto enttEntity = internal::fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();
        if (registry.valid(enttEntity) && registry.all_of<components::PhysicsAnimationComponent>(enttEntity))
        {
            return registry.get<components::PhysicsAnimationComponent>(enttEntity).currentMode;
        }
        return types::PhysicsAnimationMode::Animated;
    }

    bool PhysicsAnimationServiceImpl::hasPhysicsAnimation(EntityHandle entity) const
    {
        return physicsProvider && physicsProvider->hasPhysicsAnimation(entity);
    }

    void PhysicsAnimationServiceImpl::applyRagdollImpulse(EntityHandle entity, const glm::vec3& impulse)
    {
        if (physicsProvider) physicsProvider->applyRagdollImpulse(entity, impulse);
    }

    void PhysicsAnimationServiceImpl::applyRagdollBoneImpulse(EntityHandle entity, int animBoneIndex,
                                                               const glm::vec3& impulse)
    {
        if (physicsProvider) physicsProvider->applyRagdollBoneImpulse(entity, animBoneIndex, impulse);
    }

    void PhysicsAnimationServiceImpl::setMode(EntityHandle entity, types::PhysicsAnimationMode mode)
    {
        if (!physicsProvider || !physicsProvider->hasPhysicsAnimation(entity)) return;

        types::PhysicsAnimationMode oldMode = getMode(entity);
        if (oldMode == mode) return;

        physicsProvider->setPhysicsAnimationMode(entity, mode);

        bool oldUsesRagdollBodies = oldMode == types::PhysicsAnimationMode::Ragdoll ||
                                    oldMode == types::PhysicsAnimationMode::PoweredRagdoll;
        bool newUsesRagdollBodies = mode == types::PhysicsAnimationMode::Ragdoll ||
                                    mode == types::PhysicsAnimationMode::PoweredRagdoll;

        if (!oldUsesRagdollBodies && newUsesRagdollBodies)
        {
            events::physicsAnimation::RagdollActivatedNotification notification;
            notification.entity = entity;
            ::events::EventDispatcher::instance().publish(notification);
        }
        else if (oldUsesRagdollBodies && !newUsesRagdollBodies)
        {
            events::physicsAnimation::RagdollDeactivatedNotification notification;
            notification.entity = entity;
            ::events::EventDispatcher::instance().publish(notification);
        }
    }

    void PhysicsAnimationServiceImpl::setBoneMotorStrength(EntityHandle entity,
                                                            const std::string& boneName, float strength)
    {
        if (physicsProvider) physicsProvider->setBoneMotorStrength(entity, boneName, strength);
    }

    void PhysicsAnimationServiceImpl::setGlobalMotorStrength(EntityHandle entity, float strength)
    {
        if (physicsProvider) physicsProvider->setGlobalMotorStrength(entity, strength);
    }

    void PhysicsAnimationServiceImpl::applyHitReaction(EntityHandle entity, const std::string& boneName,
                                                        const glm::vec3& impulse, float recoverTime)
    {
        if (physicsProvider) physicsProvider->applyHitReaction(entity, boneName, impulse, recoverTime);
    }

    bool PhysicsAnimationServiceImpl::isRagdollSettled(EntityHandle entity) const
    {
        return physicsProvider && physicsProvider->isRagdollSettled(entity);
    }
}
