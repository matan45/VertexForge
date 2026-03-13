#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Skeleton/SkeletonPose.h>
#include <Jolt/Physics/Body/BodyID.h>
#include "PhysicsSkeletonConverter.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace core::physics
{
    struct PhysicsContext;
    class PhysicsBodyRegistry;
    struct RagdollBuildResult;

    struct RagdollInstanceData
    {
        JPH::Ref<JPH::Ragdoll> ragdoll;
        SkeletonConversionResult skeletonConversion;
        uint32_t collisionGroupId = 0;
    };

    class PhysicsRagdollManager
    {
    public:
        void init(PhysicsContext* context, PhysicsBodyRegistry* registry);
        void cleanUp();

        bool createRagdoll(uint64_t entityId, const RagdollBuildResult& buildResult);
        void destroyRagdoll(uint64_t entityId);
        bool hasRagdoll(uint64_t entityId) const;
        void activateRagdoll(uint64_t entityId);
        void deactivateRagdoll(uint64_t entityId);
        bool getRagdollPose(uint64_t entityId, JPH::SkeletonPose& outPose) const;
        void applyRagdollImpulse(uint64_t entityId, const glm::vec3& impulse);
        void applyRagdollBoneImpulse(uint64_t entityId, int physicsBoneIndex, const glm::vec3& impulse);

        bool createKinematicBoneBodies(uint64_t entityId, const RagdollBuildResult& buildResult,
                                        const glm::vec3& entityPosition = glm::vec3(0.0f));
        void destroyKinematicBoneBodies(uint64_t entityId);
        void updateKinematicBonePoses(uint64_t entityId,
                                       const std::vector<glm::mat4>& boneWorldTransforms,
                                       const std::vector<int>& physicsToAnimBoneIndex,
                                       float deltaTime);

        void transitionToRagdoll(uint64_t entityId, const JPH::SkeletonPose& currentPose);
        void transitionToKinematic(uint64_t entityId, const RagdollBuildResult& buildResult,
                                    const glm::vec3& entityPosition);

    private:
        PhysicsContext* ctx = nullptr;
        PhysicsBodyRegistry* bodyRegistry = nullptr;

        std::unordered_map<uint64_t, RagdollInstanceData> entityRagdolls;
        std::unordered_map<uint64_t, std::vector<JPH::BodyID>> entityBoneBodies;
        uint32_t nextCollisionGroupId = 1;
    };
}
