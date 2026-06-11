#include "PhysicsAdapter.hpp"
#include "../../physics/PhysicsSkeletonConverter.hpp"
#include "../../physics/RagdollSettingsBuilder.hpp"
#include "../../graphics/animation/RuntimeAnimatorSystem.hpp"
#include "../../graphics/animation/AnimatorStateMachine.hpp"
#include "../../services/events/physics/PhysicsAnimationEvents.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "components/PhysicsAnimationComponent.hpp"
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>

#include "print/Log.hpp"
namespace core
{
    namespace
    {
        // Lightweight pose blend for mode transitions; both inputs are skinning matrices
        std::vector<glm::mat4> blendPoseMatrices(const std::vector<glm::mat4>& from,
                                                 const std::vector<glm::mat4>& to, float t)
        {
            size_t count = std::min(from.size(), to.size());
            std::vector<glm::mat4> result(count);
            for (size_t i = 0; i < count; ++i)
            {
                glm::vec3 fromScale(glm::length(glm::vec3(from[i][0])), glm::length(glm::vec3(from[i][1])),
                                    glm::length(glm::vec3(from[i][2])));
                glm::vec3 toScale(glm::length(glm::vec3(to[i][0])), glm::length(glm::vec3(to[i][1])),
                                  glm::length(glm::vec3(to[i][2])));

                glm::quat fromRot = glm::normalize(glm::quat_cast(from[i]));
                glm::quat toRot = glm::normalize(glm::quat_cast(to[i]));

                glm::vec3 position = glm::mix(glm::vec3(from[i][3]), glm::vec3(to[i][3]), t);
                glm::quat rotation = glm::slerp(fromRot, toRot, t);
                glm::vec3 scale = glm::mix(fromScale, toScale, t);

                result[i] = glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) *
                            glm::scale(glm::mat4(1.0f), scale);
            }
            return result;
        }

        float smoothstep01(float t)
        {
            t = std::clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        void getEntityWorldTransform(entt::registry& registry, entt::entity entity,
                                     glm::vec3& outPosition, glm::quat& outRotation)
        {
            outPosition = glm::vec3(0.0f);
            outRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            if (registry.all_of<components::TransformComponent>(entity))
            {
                const auto& transform = registry.get<components::TransformComponent>(entity);
                outPosition = transform.position;
                outRotation = glm::quat(glm::radians(transform.rotation));
            }
        }
    }

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
        resolveMotorProfile(state, config);
        physicsAnimationEntities[entity.id] = std::move(state);

        return true;
    }

    void PhysicsAdapter::resolveMotorProfile(PhysicsAnimationState& state,
                                              const types::PhysicsAnimationConfig& config) const
    {
        const auto& conversion = state.buildResult.skeletonConversion;
        size_t boneCount = conversion.physicsToAnimBoneIndex.size();
        state.profileStrengths.assign(boneCount, config.defaultMotorStrength);
        state.maxTorques.assign(boneCount, config.defaultMotorMaxTorque);

        for (size_t i = 0; i < boneCount; ++i)
        {
            int animIdx = conversion.physicsToAnimBoneIndex[i];
            if (animIdx < 0 || animIdx >= static_cast<int>(state.skeletonData.bones.size())) continue;

            const types::BoneMotorSettings* motor =
                config.findBoneMotor(state.skeletonData.bones[animIdx].name);
            if (motor)
            {
                state.profileStrengths[i] = std::clamp(motor->strength, 0.0f, 1.0f);
                state.maxTorques[i] = motor->maxTorque;
            }
        }
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

    void PhysicsAdapter::setPhysicsAnimationMode(services::EntityHandle entity,
                                                  types::PhysicsAnimationMode newMode)
    {
        if (!physicsWorld) return;
        auto it = physicsAnimationEntities.find(entity.id);
        if (it == physicsAnimationEntities.end()) return;
        auto& state = it->second;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        if (!registry.valid(enttEntity) ||
            !registry.all_of<components::PhysicsAnimationComponent>(enttEntity))
            return;

        auto& physAnimComp = registry.get<components::PhysicsAnimationComponent>(enttEntity);
        types::PhysicsAnimationMode oldMode = physAnimComp.currentMode;
        if (oldMode == newMode) return;

        glm::vec3 entityPos;
        glm::quat entityRot;
        getEntityWorldTransform(registry, enttEntity, entityPos, entityRot);

        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(enttEntity);
        const auto& conversion = state.buildResult.skeletonConversion;

        bool oldUsesRagdollBodies = oldMode == types::PhysicsAnimationMode::Ragdoll ||
                                    oldMode == types::PhysicsAnimationMode::PoweredRagdoll;
        bool newUsesRagdollBodies = newMode == types::PhysicsAnimationMode::Ragdoll ||
                                    newMode == types::PhysicsAnimationMode::PoweredRagdoll;

        if (!oldUsesRagdollBodies && newUsesRagdollBodies)
        {
            // Spawn ragdoll bodies at the current animated pose
            physicsWorld->destroyKinematicBoneBodies(entity.id);

            JPH::SkeletonPose currentPose;
            if (animator && animator->isInitialized() && !animator->getBoneMatrices().empty())
            {
                currentPose = physics::PhysicsSkeletonConverter::buildTargetPoseFromAnimatorMatrices(
                    conversion.physicsSkeleton, animator->getBoneMatrices(),
                    conversion.physicsToAnimBoneIndex, state.skeletonData, entityPos, entityRot);
            }
            else
            {
                // No evaluated pose yet: bind pose (bindPoses are model-space bind transforms)
                currentPose = physics::PhysicsSkeletonConverter::buildPhysicsSkeletonPose(
                    conversion.physicsSkeleton, state.skeletonData.bindPoses,
                    conversion.physicsToAnimBoneIndex, entityPos);
            }
            physicsWorld->transitionToRagdoll(entity.id, currentPose);
        }
        else if (oldUsesRagdollBodies && !newUsesRagdollBodies)
        {
            // Capture the last physics-driven pose so it can crossfade back to animation
            if (!physAnimComp.overrideBoneMatrices.empty())
            {
                physAnimComp.capturedPoseMatrices = physAnimComp.overrideBoneMatrices;
                physAnimComp.blendOutProgress = 0.0f;
            }
            physicsWorld->deactivateRagdoll(entity.id);
            if (newMode == types::PhysicsAnimationMode::Kinematic)
            {
                physicsWorld->createKinematicBoneBodies(entity.id, state.buildResult, entityPos);
            }
        }
        else if (!oldUsesRagdollBodies && !newUsesRagdollBodies)
        {
            if (newMode == types::PhysicsAnimationMode::Kinematic)
            {
                physicsWorld->createKinematicBoneBodies(entity.id, state.buildResult, entityPos);
            }
        }
        // PoweredRagdoll <-> Ragdoll: bodies stay in place, only motors change below

        if (newMode == types::PhysicsAnimationMode::Ragdoll)
        {
            physicsWorld->setRagdollMotorsOff(entity.id);
            if (animator) animator->pause();
        }
        else
        {
            // Powered ragdoll needs the animator producing target poses every frame
            if (animator) animator->play();
        }

        if (newUsesRagdollBodies)
        {
            state.settleFrames = 0;
            state.settledNotified = false;
            physAnimComp.ragdollSettled = false;
        }
        if (newMode == types::PhysicsAnimationMode::PoweredRagdoll)
        {
            state.blendInProgress = oldMode == types::PhysicsAnimationMode::Ragdoll ? 1.0f : 0.0f;
        }
        if (!newUsesRagdollBodies)
        {
            physAnimComp.overrideBoneMatrices.clear();
        }

        physAnimComp.currentMode = newMode;
    }

    void PhysicsAdapter::setBoneMotorStrength(services::EntityHandle entity, const std::string& boneName,
                                               float strength)
    {
        auto it = physicsAnimationEntities.find(entity.id);
        if (it == physicsAnimationEntities.end()) return;
        auto& state = it->second;

        int animIdx = state.skeletonData.getBoneIndex(boneName);
        if (animIdx < 0) return;

        const auto& convMap = state.buildResult.skeletonConversion.animToPhysicsBoneIndex;
        auto mapIt = convMap.find(animIdx);
        if (mapIt == convMap.end()) return;

        if (mapIt->second >= 0 && mapIt->second < static_cast<int>(state.profileStrengths.size()))
        {
            state.profileStrengths[mapIt->second] = std::clamp(strength, 0.0f, 1.0f);
        }
    }

    void PhysicsAdapter::setGlobalMotorStrength(services::EntityHandle entity, float strength)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        if (registry.valid(enttEntity) &&
            registry.all_of<components::PhysicsAnimationComponent>(enttEntity))
        {
            registry.get<components::PhysicsAnimationComponent>(enttEntity).globalMotorStrength =
                std::clamp(strength, 0.0f, 1.0f);
        }
    }

    void PhysicsAdapter::applyHitReaction(services::EntityHandle entity, const std::string& boneName,
                                           const glm::vec3& impulse, float recoverTime)
    {
        if (!physicsWorld) return;
        auto it = physicsAnimationEntities.find(entity.id);
        if (it == physicsAnimationEntities.end()) return;
        auto& state = it->second;

        int animIdx = state.skeletonData.getBoneIndex(boneName);
        if (animIdx < 0) return;

        const auto& conversion = state.buildResult.skeletonConversion;
        auto mapIt = conversion.animToPhysicsBoneIndex.find(animIdx);
        if (mapIt == conversion.animToPhysicsBoneIndex.end()) return;
        int physicsBone = mapIt->second;

        physicsWorld->applyRagdollBoneImpulse(entity.id, physicsBone, impulse);

        auto& registry = scene::EntityRegistry::getRegistry();
        auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        if (!registry.valid(enttEntity) ||
            !registry.all_of<components::PhysicsAnimationComponent>(enttEntity))
            return;
        auto& physAnimComp = registry.get<components::PhysicsAnimationComponent>(enttEntity);
        const auto& hitConfig = physAnimComp.config.hitReaction;

        // Parent indices of the physics skeleton, for descendant chain resolution
        const JPH::Skeleton* physicsSkeleton = conversion.physicsSkeleton;
        std::vector<int> parentIndices(physicsSkeleton->GetJointCount());
        for (int i = 0; i < physicsSkeleton->GetJointCount(); ++i)
            parentIndices[i] = physicsSkeleton->GetJoint(i).mParentJointIndex;

        ::physics::HitReactionInstance instance;
        instance.affectedPhysicsBones =
            ::physics::HitReactionState::resolveChain(physicsBone, parentIndices, hitConfig.chainDepth);
        instance.recoverTime = recoverTime >= 0.0f ? recoverTime : hitConfig.defaultRecoverTime;
        instance.dipStrength = std::clamp(hitConfig.strengthDip, 0.0f, 1.0f);
        state.hitReactions.active.push_back(std::move(instance));

        state.settleFrames = 0;
        state.settledNotified = false;
        physAnimComp.ragdollSettled = false;
    }

    bool PhysicsAdapter::isRagdollSettled(services::EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        if (registry.valid(enttEntity) &&
            registry.all_of<components::PhysicsAnimationComponent>(enttEntity))
        {
            return registry.get<components::PhysicsAnimationComponent>(enttEntity).ragdollSettled;
        }
        return false;
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

                updateSettleDetection(entityId, state, physAnimComp);
            }
            else if (physAnimComp.currentMode == types::PhysicsAnimationMode::PoweredRagdoll)
            {
                const auto& conversion = state.buildResult.skeletonConversion;
                auto* animator = animatorSystem.getAnimator(entity);

                std::vector<glm::mat4> animMatrices;
                if (animator && animator->isInitialized())
                {
                    animMatrices = animator->getBoneMatrices();
                }

                // Drive joint motors toward last frame's animator pose; targets are
                // consumed by the next physics step
                if (!animMatrices.empty())
                {
                    glm::vec3 entityPos;
                    glm::quat entityRot;
                    getEntityWorldTransform(registry, entity, entityPos, entityRot);

                    JPH::SkeletonPose targetPose =
                        physics::PhysicsSkeletonConverter::buildTargetPoseFromAnimatorMatrices(
                            conversion.physicsSkeleton, animMatrices,
                            conversion.physicsToAnimBoneIndex, state.skeletonData,
                            entityPos, entityRot);

                    state.hitReactions.update(deltaTime);
                    std::vector<float> effectiveStrengths = ::physics::resolveEffectiveStrengths(
                        state.profileStrengths, state.hitReactions, physAnimComp.globalMotorStrength);

                    physicsWorld->driveRagdollToPose(entityId, targetPose, effectiveStrengths,
                                                     state.maxTorques);
                    physicsWorld->driveRagdollRoot(
                        entityId, targetPose,
                        physAnimComp.config.rootMotorStrength * physAnimComp.globalMotorStrength,
                        deltaTime);
                }

                // Visual pose comes from the ragdoll (motors make it track the animation)
                JPH::SkeletonPose ragdollPose;
                ragdollPose.SetSkeleton(conversion.physicsSkeleton);
                if (physicsWorld->getRagdollPose(entityId, ragdollPose))
                {
                    auto ragdollMatrices = physics::PhysicsSkeletonConverter::ragdollPoseToSkinningMatrices(
                        ragdollPose, conversion, state.skeletonData, animMatrices);

                    if (state.blendInProgress < 1.0f && !animMatrices.empty())
                    {
                        state.blendInProgress += deltaTime /
                            std::max(physAnimComp.config.poweredBlendInTime, 0.001f);
                        state.blendInProgress = std::min(state.blendInProgress, 1.0f);
                        physAnimComp.overrideBoneMatrices = blendPoseMatrices(
                            animMatrices, ragdollMatrices, smoothstep01(state.blendInProgress));
                    }
                    else
                    {
                        physAnimComp.overrideBoneMatrices = std::move(ragdollMatrices);
                    }
                }

                updateSettleDetection(entityId, state, physAnimComp);
            }
            else if (physAnimComp.blendOutProgress < 1.0f)
            {
                // Ragdoll -> Animated crossfade from the captured physics pose
                auto* animator = animatorSystem.getAnimator(entity);
                std::vector<glm::mat4> animMatrices;
                if (animator && animator->isInitialized())
                {
                    animMatrices = animator->getBoneMatrices();
                }

                physAnimComp.blendOutProgress += deltaTime /
                    std::max(physAnimComp.config.ragdollToAnimatedBlendTime, 0.001f);

                if (physAnimComp.blendOutProgress >= 1.0f || animMatrices.empty() ||
                    physAnimComp.capturedPoseMatrices.empty())
                {
                    physAnimComp.blendOutProgress = 1.0f;
                    physAnimComp.capturedPoseMatrices.clear();
                    physAnimComp.overrideBoneMatrices.clear();
                }
                else
                {
                    physAnimComp.overrideBoneMatrices = blendPoseMatrices(
                        physAnimComp.capturedPoseMatrices, animMatrices,
                        smoothstep01(physAnimComp.blendOutProgress));
                }
            }
        }
    }

    void PhysicsAdapter::updateSettleDetection(uint64_t entityId, PhysicsAnimationState& state,
                                                components::PhysicsAnimationComponent& physAnimComp)
    {
        const auto& config = physAnimComp.config;
        if (physicsWorld->isRagdollBelowVelocityThreshold(entityId,
                                                          config.settleLinearVelocityThreshold,
                                                          config.settleAngularVelocityThreshold))
        {
            ++state.settleFrames;
            if (state.settleFrames >= config.settleFrameCount && !state.settledNotified)
            {
                state.settledNotified = true;
                physAnimComp.ragdollSettled = true;

                events::physicsAnimation::RagdollSettledNotification notification;
                notification.entity = services::EntityHandle{entityId};
                ::events::EventDispatcher::instance().publish(notification);
            }
        }
        else
        {
            state.settleFrames = 0;
            state.settledNotified = false;
            physAnimComp.ragdollSettled = false;
        }
    }
}
