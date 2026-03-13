#include "PhysicsRagdollManager.hpp"
#include "PhysicsContext.hpp"
#include "PhysicsBodyRegistry.hpp"
#include "RagdollSettingsBuilder.hpp"
#include "JoltConversions.hpp"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
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
