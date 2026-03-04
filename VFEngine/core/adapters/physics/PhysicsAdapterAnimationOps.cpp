#include "PhysicsAdapter.hpp"
#include "../../physics/PhysicsSkeletonConverter.hpp"
#include "../../physics/RagdollSettingsBuilder.hpp"
#include "../../graphics/animation/RuntimeAnimatorSystem.hpp"
#include "../../graphics/animation/AnimatorStateMachine.hpp"
#include "components/PhysicsAnimationComponent.hpp"
#include "scene/EntityRegistry.hpp"

#include "print/Log.hpp"
namespace core
{
    bool PhysicsAdapter::createPhysicsAnimation(services::EntityHandle entity,
                                                 const types::PhysicsAnimationConfig& config,
                                                 const resource::SkeletonData& skeletonData,
                                                 const glm::vec3& entityPosition,
                                                 const glm::quat& entityRotation)
    {
        if (!physicsWorld) return false;

        auto buildResult = physics::RagdollSettingsBuilder::build(config, skeletonData, entityPosition, entityRotation);
        if (!buildResult.success)
        {
            vfLogWarning("Failed to build ragdoll settings for entity {}: {}", entity.id, buildResult.errorMessage);
            return false;
        }

        if (!physicsWorld->createRagdoll(entity.id, buildResult))
        {
            vfLogWarning("Failed to create ragdoll in PhysicsWorld for entity {}", entity.id);
            return false;
        }

        PhysicsAnimationState state;
        state.buildResult = std::move(buildResult);
        state.skeletonData = skeletonData;
        physicsAnimationEntities[entity.id] = std::move(state);

        return true;
    }

    void PhysicsAdapter::destroyPhysicsAnimation(services::EntityHandle entity)
    {
        if (!physicsWorld) return;
        physicsWorld->destroyKinematicBoneBodies(entity.id);
        physicsWorld->destroyRagdoll(entity.id);
        physicsAnimationEntities.erase(entity.id);
    }

    bool PhysicsAdapter::hasPhysicsAnimation(services::EntityHandle entity) const
    {
        return physicsAnimationEntities.contains(entity.id);
    }

    void PhysicsAdapter::activateRagdoll(services::EntityHandle entity)
    {
        if (physicsWorld) physicsWorld->activateRagdoll(entity.id);
    }

    void PhysicsAdapter::deactivateRagdoll(services::EntityHandle entity)
    {
        if (physicsWorld) physicsWorld->deactivateRagdoll(entity.id);
    }

    bool PhysicsAdapter::isRagdollActive(services::EntityHandle entity) const
    {
        return physicsWorld && physicsWorld->hasRagdoll(entity.id);
    }

    bool PhysicsAdapter::createKinematicBones(services::EntityHandle entity, const glm::vec3& entityPosition)
    {
        if (!physicsWorld) return false;
        auto it = physicsAnimationEntities.find(entity.id);
        if (it == physicsAnimationEntities.end()) return false;
        return physicsWorld->createKinematicBoneBodies(entity.id, it->second.buildResult, entityPosition);
    }

    void PhysicsAdapter::destroyKinematicBones(services::EntityHandle entity)
    {
        if (physicsWorld) physicsWorld->destroyKinematicBoneBodies(entity.id);
    }

    void PhysicsAdapter::updateKinematicBones(services::EntityHandle entity,
                                               const std::vector<glm::mat4>& boneWorldTransforms,
                                               float deltaTime)
    {
        if (!physicsWorld) return;
        auto it = physicsAnimationEntities.find(entity.id);
        if (it == physicsAnimationEntities.end()) return;

        const auto& conversion = it->second.buildResult.skeletonConversion;
        physicsWorld->updateKinematicBonePoses(entity.id, boneWorldTransforms,
                                               conversion.physicsToAnimBoneIndex, deltaTime);
    }

    std::vector<glm::mat4> PhysicsAdapter::getRagdollBoneMatrices(
        services::EntityHandle entity,
        const resource::SkeletonData& skeletonData,
        const std::vector<glm::mat4>& fallbackAnimWorldTransforms) const
    {
        if (!physicsWorld) return {};

        auto it = physicsAnimationEntities.find(entity.id);
        if (it == physicsAnimationEntities.end()) return {};

        const auto& conversion = it->second.buildResult.skeletonConversion;
        JPH::SkeletonPose ragdollPose;
        ragdollPose.SetSkeleton(conversion.physicsSkeleton);

        if (!physicsWorld->getRagdollPose(entity.id, ragdollPose)) return {};

        return physics::PhysicsSkeletonConverter::ragdollPoseToSkinningMatrices(
            ragdollPose, conversion, skeletonData, fallbackAnimWorldTransforms);
    }

    void PhysicsAdapter::transitionToRagdoll(services::EntityHandle entity,
                                              const std::vector<glm::mat4>& currentBoneWorldTransforms,
                                              const glm::vec3& entityPosition)
    {
        if (!physicsWorld) return;
        auto it = physicsAnimationEntities.find(entity.id);
        if (it == physicsAnimationEntities.end()) return;

        const auto& conversion = it->second.buildResult.skeletonConversion;
        JPH::SkeletonPose currentPose = physics::PhysicsSkeletonConverter::buildPhysicsSkeletonPose(
            conversion.physicsSkeleton, currentBoneWorldTransforms,
            conversion.physicsToAnimBoneIndex, entityPosition);

        physicsWorld->transitionToRagdoll(entity.id, currentPose);

        auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(enttEntity);
        if (animator) animator->pause();
    }

    void PhysicsAdapter::transitionToKinematic(services::EntityHandle entity, const glm::vec3& entityPosition)
    {
        if (!physicsWorld) return;
        auto it = physicsAnimationEntities.find(entity.id);
        if (it == physicsAnimationEntities.end()) return;

        physicsWorld->transitionToKinematic(entity.id, it->second.buildResult, entityPosition);

        auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(enttEntity);
        if (animator) animator->play();
    }

    void PhysicsAdapter::applyRagdollImpulse(services::EntityHandle entity, const glm::vec3& impulse)
    {
        if (physicsWorld) physicsWorld->applyRagdollImpulse(entity.id, impulse);
    }

    void PhysicsAdapter::applyRagdollBoneImpulse(services::EntityHandle entity, int animBoneIndex,
                                                  const glm::vec3& impulse)
    {
        if (!physicsWorld) return;
        auto it = physicsAnimationEntities.find(entity.id);
        if (it == physicsAnimationEntities.end()) return;

        const auto& convMap = it->second.buildResult.skeletonConversion.animToPhysicsBoneIndex;
        auto mapIt = convMap.find(animBoneIndex);
        if (mapIt == convMap.end()) return;

        physicsWorld->applyRagdollBoneImpulse(entity.id, mapIt->second, impulse);
    }

    void PhysicsAdapter::updatePhysicsAnimations(float deltaTime)
    {
        if (!physicsWorld || physicsAnimationEntities.empty()) return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& animatorSystem = animation::RuntimeAnimatorSystem::instance();

        for (auto& [entityId, state] : physicsAnimationEntities)
        {
            auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
            if (!registry.valid(entity)) continue;
            if (!registry.all_of<components::PhysicsAnimationComponent>(entity)) continue;

            auto& physAnimComp = registry.get<components::PhysicsAnimationComponent>(entity);
            if (!physAnimComp.isInitialized) continue;

            if (physAnimComp.currentMode == types::PhysicsAnimationMode::Kinematic)
            {
                auto* animator = animatorSystem.getAnimator(entity);
                if (animator && animator->isInitialized())
                {
                    const auto& boneMatrices = animator->getBoneMatrices();
                    if (!boneMatrices.empty())
                    {
                        const auto& conversion = state.buildResult.skeletonConversion;
                        physicsWorld->updateKinematicBonePoses(
                            entityId, boneMatrices,
                            conversion.physicsToAnimBoneIndex, deltaTime);
                    }
                }
            }
            else if (physAnimComp.currentMode == types::PhysicsAnimationMode::Ragdoll)
            {
                const auto& conversion = state.buildResult.skeletonConversion;
                JPH::SkeletonPose ragdollPose;
                ragdollPose.SetSkeleton(conversion.physicsSkeleton);

                if (physicsWorld->getRagdollPose(entityId, ragdollPose))
                {
                    std::vector<glm::mat4> fallbackMatrices;
                    auto* animator = animatorSystem.getAnimator(entity);
                    if (animator && animator->isInitialized())
                    {
                        fallbackMatrices = animator->getBoneMatrices();
                    }

                    physAnimComp.overrideBoneMatrices =
                        physics::PhysicsSkeletonConverter::ragdollPoseToSkinningMatrices(
                            ragdollPose, conversion, state.skeletonData, fallbackMatrices);
                }
            }
        }
    }
}
