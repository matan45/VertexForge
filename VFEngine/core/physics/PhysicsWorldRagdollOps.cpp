#include "PhysicsWorld.hpp"
#include "RagdollSettingsBuilder.hpp"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include "print/Logger.hpp"

namespace core::physics
{
    namespace
    {
        void removeAndDestroyBody(JPH::BodyInterface& bi, JPH::BodyID bodyId)
        {
            if (bi.IsAdded(bodyId)) bi.RemoveBody(bodyId);
            bi.DestroyBody(bodyId);
        }
    }

    bool PhysicsWorld::createRagdoll(uint64_t entityId, const RagdollBuildResult& buildResult)
    {
        if (!initialized || !physicsSystem || !buildResult.success || !buildResult.settings)
        {
            return false;
        }

        if (entityRagdolls.find(entityId) != entityRagdolls.end())
        {
            loggerWarning("Entity {}: Ragdoll already exists, destroying first", entityId);
            destroyRagdoll(entityId);
        }

        uint32_t groupId = nextCollisionGroupId++;
        JPH::Ragdoll* ragdoll = buildResult.settings->CreateRagdoll(groupId, entityId, physicsSystem.get());
        if (!ragdoll)
        {
            loggerError("Entity {}: Failed to create ragdoll instance", entityId);
            return false;
        }

        for (size_t i = 0; i < ragdoll->GetBodyCount(); ++i)
        {
            JPH::BodyID bodyId = ragdoll->GetBodyID(static_cast<int>(i));
            if (!bodyId.IsInvalid())
            {
                bodyToEntity[bodyId.GetIndex()] = entityId;
                bodyToBoneIndex[bodyId.GetIndex()] = static_cast<int>(i);
            }
        }

        RagdollInstanceData data;
        data.ragdoll = ragdoll;
        data.skeletonConversion = buildResult.skeletonConversion;
        data.collisionGroupId = groupId;
        entityRagdolls[entityId] = std::move(data);

        return true;
    }

    void PhysicsWorld::destroyRagdoll(uint64_t entityId)
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
                    bodyToEntity.erase(bodyId.GetIndex());
                    bodyToBoneIndex.erase(bodyId.GetIndex());
                }
            }
            ragdollData.ragdoll->RemoveFromPhysicsSystem();
        }

        entityRagdolls.erase(it);
    }

    bool PhysicsWorld::hasRagdoll(uint64_t entityId) const
    {
        return entityRagdolls.find(entityId) != entityRagdolls.end();
    }

    void PhysicsWorld::activateRagdoll(uint64_t entityId)
    {
        auto it = entityRagdolls.find(entityId);
        if (it == entityRagdolls.end() || !it->second.ragdoll) return;

        it->second.ragdoll->AddToPhysicsSystem(JPH::EActivation::Activate);

        for (int i = 0; i < static_cast<int>(it->second.ragdoll->GetBodyCount()); ++i)
        {
            JPH::BodyID bodyId = it->second.ragdoll->GetBodyID(i);
            if (!bodyId.IsInvalid())
            {
                bodyToEntity[bodyId.GetIndex()] = entityId;
                bodyToBoneIndex[bodyId.GetIndex()] = i;
            }
        }
    }

    void PhysicsWorld::deactivateRagdoll(uint64_t entityId)
    {
        auto it = entityRagdolls.find(entityId);
        if (it == entityRagdolls.end() || !it->second.ragdoll) return;

        for (int i = 0; i < static_cast<int>(it->second.ragdoll->GetBodyCount()); ++i)
        {
            JPH::BodyID bodyId = it->second.ragdoll->GetBodyID(i);
            if (!bodyId.IsInvalid())
            {
                bodyToEntity.erase(bodyId.GetIndex());
                bodyToBoneIndex.erase(bodyId.GetIndex());
            }
        }

        it->second.ragdoll->RemoveFromPhysicsSystem();
    }

    bool PhysicsWorld::getRagdollPose(uint64_t entityId, JPH::SkeletonPose& outPose) const
    {
        auto it = entityRagdolls.find(entityId);
        if (it == entityRagdolls.end() || !it->second.ragdoll) return false;

        it->second.ragdoll->GetPose(outPose);
        return true;
    }

    void PhysicsWorld::applyRagdollImpulse(uint64_t entityId, const glm::vec3& impulse)
    {
        auto it = entityRagdolls.find(entityId);
        if (it == entityRagdolls.end() || !it->second.ragdoll) return;

        it->second.ragdoll->AddImpulse(toJolt(impulse));
    }

    void PhysicsWorld::applyRagdollBoneImpulse(uint64_t entityId, int physicsBoneIndex, const glm::vec3& impulse)
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
            physicsSystem->GetBodyInterface().AddImpulse(bodyId, toJolt(impulse));
        }
    }

    bool PhysicsWorld::createKinematicBoneBodies(uint64_t entityId, const RagdollBuildResult& buildResult,
                                                  const glm::vec3& entityPosition)
    {
        if (!initialized || !physicsSystem || !buildResult.success || !buildResult.settings)
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

        auto& bodyInterface = physicsSystem->GetBodyInterface();

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
                bodyToEntity[bodyId.GetIndex()] = entityId;
                bodyToBoneIndex[bodyId.GetIndex()] = i;
                boneBodies.push_back(bodyId);
            }
            else
            {
                loggerWarning("Entity {}: Failed to create kinematic bone body {}", entityId, i);
                boneBodies.push_back(JPH::BodyID());
            }
        }

        entityBoneBodies[entityId] = std::move(boneBodies);
        return true;
    }

    void PhysicsWorld::destroyKinematicBoneBodies(uint64_t entityId)
    {
        auto it = entityBoneBodies.find(entityId);
        if (it == entityBoneBodies.end()) return;

        auto& bodyInterface = physicsSystem->GetBodyInterface();
        for (auto& bodyId : it->second)
        {
            if (!bodyId.IsInvalid())
            {
                bodyToEntity.erase(bodyId.GetIndex());
                bodyToBoneIndex.erase(bodyId.GetIndex());
                removeAndDestroyBody(bodyInterface, bodyId);
            }
        }

        entityBoneBodies.erase(it);
    }

    void PhysicsWorld::updateKinematicBonePoses(uint64_t entityId,
                                                  const std::vector<glm::mat4>& boneWorldTransforms,
                                                  const std::vector<int>& physicsToAnimBoneIndex,
                                                  float deltaTime)
    {
        auto it = entityBoneBodies.find(entityId);
        if (it == entityBoneBodies.end()) return;

        auto& bodyInterface = physicsSystem->GetBodyInterface();
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

    void PhysicsWorld::transitionToRagdoll(uint64_t entityId, const JPH::SkeletonPose& currentPose)
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

    void PhysicsWorld::transitionToKinematic(uint64_t entityId, const RagdollBuildResult& buildResult,
                                              const glm::vec3& entityPosition)
    {
        deactivateRagdoll(entityId);
        createKinematicBoneBodies(entityId, buildResult, entityPosition);
    }

}
