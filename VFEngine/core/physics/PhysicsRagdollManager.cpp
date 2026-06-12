#include "PhysicsRagdollManager.hpp"
#include "PhysicsContext.hpp"
#include "PhysicsBodyRegistry.hpp"
#include "RagdollSettingsBuilder.hpp"
#include "JoltConversions.hpp"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include "print/Log.hpp"

namespace core::physics
{
    void PhysicsRagdollManager::init(PhysicsContext* context, PhysicsBodyRegistry* registry)
    {
        ctx = context;
        bodyRegistry = registry;
    }

    void PhysicsRagdollManager::cleanUp()
    {
        if (!ctx || !ctx->physicsSystem) return;

        auto& bodyInterface = ctx->getBodyInterface();

        for (auto& [entityId, ragdollData] : entityRagdolls)
        {
            if (ragdollData.ragdoll)
            {
                ragdollData.ragdoll->RemoveFromPhysicsSystem();
            }
        }
        entityRagdolls.clear();

        for (auto& [entityId, boneBodies] : entityBoneBodies)
        {
            for (auto& bodyId : boneBodies)
            {
                if (!bodyId.IsInvalid())
                {
                    bodyRegistry->unregisterBoneIndex(bodyId);
                    removeAndDestroyBody(bodyInterface, bodyId);
                }
            }
        }
        entityBoneBodies.clear();
    }

    bool PhysicsRagdollManager::createRagdoll(uint64_t entityId, const RagdollBuildResult& buildResult)
    {
        if (!ctx || !ctx->physicsSystem || !buildResult.success || !buildResult.settings)
        {
            return false;
        }

        if (entityRagdolls.find(entityId) != entityRagdolls.end())
        {
            vfLogWarning("Entity {}: Ragdoll already exists, destroying first", entityId);
            destroyRagdoll(entityId);
        }

        uint32_t groupId = nextCollisionGroupId++;
        JPH::Ragdoll* ragdoll = buildResult.settings->CreateRagdoll(groupId, entityId, ctx->physicsSystem);
        if (!ragdoll)
        {
            vfLogError("Entity {}: Failed to create ragdoll instance", entityId);
            return false;
        }

        for (size_t i = 0; i < ragdoll->GetBodyCount(); ++i)
        {
            JPH::BodyID bodyId = ragdoll->GetBodyID(static_cast<int>(i));
            if (!bodyId.IsInvalid())
            {
                bodyRegistry->registerBody(entityId, bodyId);
                bodyRegistry->registerBoneIndex(bodyId, static_cast<int>(i));
            }
        }

        RagdollInstanceData data;
        data.ragdoll = ragdoll;
        data.skeletonConversion = buildResult.skeletonConversion;
        data.collisionGroupId = groupId;
        entityRagdolls[entityId] = std::move(data);

        return true;
    }

    void PhysicsRagdollManager::destroyRagdoll(uint64_t entityId)
    {
        auto it = entityRagdolls.find(entityId);
        if (it == entityRagdolls.end()) return;

        auto& ragdollData = it->second;
        if (ragdollData.ragdoll)
        {
            for (size_t i = 0; i < ragdollData.ragdoll->GetBodyCount(); ++i)
            {
                JPH::BodyID bodyId = ragdollData.ragdoll->GetBodyID(static_cast<int>(i));
                if (!bodyId.IsInvalid())
                {
                    bodyRegistry->unregisterBoneIndex(bodyId);
                }
            }
            ragdollData.ragdoll->RemoveFromPhysicsSystem();
        }

        entityRagdolls.erase(it);
    }

    bool PhysicsRagdollManager::hasRagdoll(uint64_t entityId) const
    {
        return entityRagdolls.find(entityId) != entityRagdolls.end();
    }

    void PhysicsRagdollManager::activateRagdoll(uint64_t entityId)
    {
        auto it = entityRagdolls.find(entityId);
        if (it == entityRagdolls.end() || !it->second.ragdoll) return;

        it->second.ragdoll->AddToPhysicsSystem(JPH::EActivation::Activate);

        for (int i = 0; i < static_cast<int>(it->second.ragdoll->GetBodyCount()); ++i)
        {
            JPH::BodyID bodyId = it->second.ragdoll->GetBodyID(i);
            if (!bodyId.IsInvalid())
            {
                bodyRegistry->registerBody(entityId, bodyId);
                bodyRegistry->registerBoneIndex(bodyId, i);
            }
        }
    }

    void PhysicsRagdollManager::deactivateRagdoll(uint64_t entityId)
    {
        auto it = entityRagdolls.find(entityId);
        if (it == entityRagdolls.end() || !it->second.ragdoll) return;

        for (int i = 0; i < static_cast<int>(it->second.ragdoll->GetBodyCount()); ++i)
        {
            JPH::BodyID bodyId = it->second.ragdoll->GetBodyID(i);
            if (!bodyId.IsInvalid())
            {
                bodyRegistry->unregisterBoneIndex(bodyId);
            }
        }

        it->second.ragdoll->RemoveFromPhysicsSystem();
    }

    bool PhysicsRagdollManager::getRagdollPose(uint64_t entityId, JPH::SkeletonPose& outPose) const
    {
        auto it = entityRagdolls.find(entityId);
        if (it == entityRagdolls.end() || !it->second.ragdoll) return false;

        it->second.ragdoll->GetPose(outPose);
        return true;
    }

    void PhysicsRagdollManager::applyRagdollImpulse(uint64_t entityId, const glm::vec3& impulse)
    {
        auto it = entityRagdolls.find(entityId);
        if (it == entityRagdolls.end() || !it->second.ragdoll) return;

        it->second.ragdoll->AddImpulse(toJolt(impulse));
    }

    void PhysicsRagdollManager::applyRagdollBoneImpulse(uint64_t entityId, int physicsBoneIndex, const glm::vec3& impulse)
    {
        auto it = entityRagdolls.find(entityId);
        if (it == entityRagdolls.end() || !it->second.ragdoll) return;

        if (physicsBoneIndex < 0 || physicsBoneIndex >= static_cast<int>(it->second.ragdoll->GetBodyCount()))
        {
            return;
        }

        JPH::BodyID bodyId = it->second.ragdoll->GetBodyID(physicsBoneIndex);
        if (!bodyId.IsInvalid())
        {
            ctx->getBodyInterface().AddImpulse(bodyId, toJolt(impulse));
        }
    }

    void PhysicsRagdollManager::driveRagdollToPose(uint64_t entityId, const JPH::SkeletonPose& targetPose,
                                                    const std::vector<float>& perBoneStrength,
                                                    const std::vector<float>& perBoneMaxTorque)
    {
        auto it = entityRagdolls.find(entityId);
        if (it == entityRagdolls.end() || !it->second.ragdoll) return;

        JPH::Ragdoll* ragdoll = it->second.ragdoll;
        const JPH::RagdollSettings* settings = ragdoll->GetRagdollSettings();

        // Mirrors Jolt's Ragdoll::DriveToPoseUsingMotors, with per-bone motor
        // state/torque control instead of unconditional Position motors
        int boneCount = static_cast<int>(targetPose.GetJointMatrices().size());
        for (int i = 0; i < boneCount; ++i)
        {
            int constraintIdx = settings->GetConstraintIndexForBodyIndex(i);
            if (constraintIdx < 0) continue;

            JPH::TwoBodyConstraint* constraint = ragdoll->GetConstraint(constraintIdx);
            if (constraint->GetSubType() != JPH::EConstraintSubType::SwingTwist) continue;
            auto* swingTwist = static_cast<JPH::SwingTwistConstraint*>(constraint);

            float strength = i < static_cast<int>(perBoneStrength.size()) ? perBoneStrength[i] : 1.0f;
            if (strength <= 0.01f)
            {
                swingTwist->SetSwingMotorState(JPH::EMotorState::Off);
                swingTwist->SetTwistMotorState(JPH::EMotorState::Off);
                continue;
            }

            float maxTorque = i < static_cast<int>(perBoneMaxTorque.size()) ? perBoneMaxTorque[i] : 150.0f;
            swingTwist->GetSwingMotorSettings().SetTorqueLimit(maxTorque * strength);
            swingTwist->GetTwistMotorSettings().SetTorqueLimit(maxTorque * strength);
            swingTwist->SetSwingMotorState(JPH::EMotorState::Position);
            swingTwist->SetTwistMotorState(JPH::EMotorState::Position);
            swingTwist->SetTargetOrientationBS(targetPose.GetJoint(i).mRotation);
        }
    }

    void PhysicsRagdollManager::driveRagdollRoot(uint64_t entityId, const JPH::SkeletonPose& targetPose,
                                                  float strength, float deltaTime)
    {
        if (strength <= 0.01f || deltaTime <= 0.0f) return;

        auto it = entityRagdolls.find(entityId);
        if (it == entityRagdolls.end() || !it->second.ragdoll) return;

        JPH::Ragdoll* ragdoll = it->second.ragdoll;
        if (ragdoll->GetBodyCount() == 0 || targetPose.GetJointMatrices().empty()) return;

        JPH::BodyID rootBody = ragdoll->GetBodyID(0);
        if (rootBody.IsInvalid()) return;

        auto& bodyInterface = ctx->getBodyInterface();

        const JPH::Mat44& rootJoint = targetPose.GetJointMatrix(0);
        JPH::RVec3 targetPos = targetPose.GetRootOffset() + rootJoint.GetTranslation();
        JPH::Quat targetRot = rootJoint.GetQuaternion().Normalized();

        JPH::RVec3 currentPos = bodyInterface.GetPosition(rootBody);
        JPH::Quat currentRot = bodyInterface.GetRotation(rootBody);

        JPH::Vec3 linearVelocity = JPH::Vec3(targetPos - currentPos) * (strength / deltaTime);
        JPH::Vec3 angularVelocity =
            (targetRot * currentRot.Conjugated()).GetAngularVelocity(deltaTime) * strength;

        // Cap correction speed so teleports/desyncs don't launch the ragdoll
        constexpr float maxLinearCorrection = 20.0f;  // m/s
        constexpr float maxAngularCorrection = 30.0f; // rad/s
        float linearSpeed = linearVelocity.Length();
        if (linearSpeed > maxLinearCorrection)
            linearVelocity *= maxLinearCorrection / linearSpeed;
        float angularSpeed = angularVelocity.Length();
        if (angularSpeed > maxAngularCorrection)
            angularVelocity *= maxAngularCorrection / angularSpeed;

        bodyInterface.SetLinearAndAngularVelocity(rootBody, linearVelocity, angularVelocity);
    }

    void PhysicsRagdollManager::setRagdollMotorsOff(uint64_t entityId)
    {
        auto it = entityRagdolls.find(entityId);
        if (it == entityRagdolls.end() || !it->second.ragdoll) return;

        JPH::Ragdoll* ragdoll = it->second.ragdoll;
        for (int i = 0; i < static_cast<int>(ragdoll->GetConstraintCount()); ++i)
        {
            JPH::TwoBodyConstraint* constraint = ragdoll->GetConstraint(i);
            if (constraint->GetSubType() != JPH::EConstraintSubType::SwingTwist) continue;
            auto* swingTwist = static_cast<JPH::SwingTwistConstraint*>(constraint);
            swingTwist->SetSwingMotorState(JPH::EMotorState::Off);
            swingTwist->SetTwistMotorState(JPH::EMotorState::Off);
        }
    }

    bool PhysicsRagdollManager::isRagdollBelowVelocityThreshold(uint64_t entityId, float linearThreshold,
                                                                 float angularThreshold) const
    {
        auto it = entityRagdolls.find(entityId);
        if (it == entityRagdolls.end() || !it->second.ragdoll) return false;

        const JPH::Ragdoll* ragdoll = it->second.ragdoll;
        auto& bodyInterface = ctx->getBodyInterface();

        for (int i = 0; i < static_cast<int>(ragdoll->GetBodyCount()); ++i)
        {
            JPH::BodyID bodyId = ragdoll->GetBodyID(i);
            if (bodyId.IsInvalid()) continue;

            if (bodyInterface.GetLinearVelocity(bodyId).Length() > linearThreshold) return false;
            if (bodyInterface.GetAngularVelocity(bodyId).Length() > angularThreshold) return false;
        }
        return true;
    }

    bool PhysicsRagdollManager::createKinematicBoneBodies(uint64_t entityId, const RagdollBuildResult& buildResult,
                                                            const glm::vec3& entityPosition)
    {
        if (!ctx || !ctx->physicsSystem || !buildResult.success || !buildResult.settings)
        {
            return false;
        }

        if (entityBoneBodies.find(entityId) != entityBoneBodies.end())
        {
            destroyKinematicBoneBodies(entityId);
        }

        const auto& parts = buildResult.settings->mParts;
        std::vector<JPH::BodyID> boneBodies;
        boneBodies.reserve(parts.size());

        auto& bodyInterface = ctx->getBodyInterface();

        uint32_t groupId = nextCollisionGroupId++;
        JPH::Ref<JPH::GroupFilterTable> groupFilter = new JPH::GroupFilterTable(static_cast<uint32_t>(parts.size()));

        for (uint32_t i = 0; i < static_cast<uint32_t>(parts.size()); ++i)
        {
            for (uint32_t j = i + 1; j < static_cast<uint32_t>(parts.size()); ++j)
            {
                groupFilter->DisableCollision(i, j);
            }
        }

        for (int i = 0; i < static_cast<int>(parts.size()); ++i)
        {
            const auto& part = parts[i];

            JPH::BodyCreationSettings bodySettings = part;
            bodySettings.mMotionType = JPH::EMotionType::Kinematic;
            bodySettings.mUserData = entityId;
            bodySettings.mCollisionGroup = JPH::CollisionGroup(groupFilter, groupId, static_cast<JPH::CollisionGroup::SubGroupID>(i));

            JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::Activate);
            if (!bodyId.IsInvalid())
            {
                bodyRegistry->registerBody(entityId, bodyId);
                bodyRegistry->registerBoneIndex(bodyId, i);
                boneBodies.push_back(bodyId);
            }
            else
            {
                vfLogWarning("Entity {}: Failed to create kinematic bone body {}", entityId, i);
                boneBodies.push_back(JPH::BodyID());
            }
        }

        entityBoneBodies[entityId] = std::move(boneBodies);
        return true;
    }

    void PhysicsRagdollManager::destroyKinematicBoneBodies(uint64_t entityId)
    {
        auto it = entityBoneBodies.find(entityId);
        if (it == entityBoneBodies.end()) return;

        auto& bodyInterface = ctx->getBodyInterface();
        for (auto& bodyId : it->second)
        {
            if (!bodyId.IsInvalid())
            {
                bodyRegistry->unregisterBoneIndex(bodyId);
                removeAndDestroyBody(bodyInterface, bodyId);
            }
        }

        entityBoneBodies.erase(it);
    }

    void PhysicsRagdollManager::updateKinematicBonePoses(uint64_t entityId,
                                                           const std::vector<glm::mat4>& boneWorldTransforms,
                                                           const std::vector<int>& physicsToAnimBoneIndex,
                                                           float deltaTime)
    {
        auto it = entityBoneBodies.find(entityId);
        if (it == entityBoneBodies.end()) return;

        auto& bodyInterface = ctx->getBodyInterface();
        const auto& boneBodies = it->second;

        for (size_t i = 0; i < boneBodies.size() && i < physicsToAnimBoneIndex.size(); ++i)
        {
            if (boneBodies[i].IsInvalid()) continue;

            int animIdx = physicsToAnimBoneIndex[i];
            if (animIdx < 0 || animIdx >= static_cast<int>(boneWorldTransforms.size())) continue;

            const glm::mat4& worldTransform = boneWorldTransforms[animIdx];

            glm::vec3 pos = glm::vec3(worldTransform[3]);
            glm::quat rot = glm::normalize(glm::quat_cast(worldTransform));

            bodyInterface.MoveKinematic(boneBodies[i], toJoltR(pos), toJolt(rot), deltaTime);
        }
    }

    void PhysicsRagdollManager::transitionToRagdoll(uint64_t entityId, const JPH::SkeletonPose& currentPose)
    {
        destroyKinematicBoneBodies(entityId);

        auto it = entityRagdolls.find(entityId);
        if (it != entityRagdolls.end() && it->second.ragdoll)
        {
            it->second.ragdoll->AddToPhysicsSystem(JPH::EActivation::Activate);
            it->second.ragdoll->SetPose(currentPose);
            it->second.ragdoll->ResetWarmStart();
        }
    }

    void PhysicsRagdollManager::transitionToKinematic(uint64_t entityId, const RagdollBuildResult& buildResult,
                                                        const glm::vec3& entityPosition)
    {
        deactivateRagdoll(entityId);
        createKinematicBoneBodies(entityId, buildResult, entityPosition);
    }
}
