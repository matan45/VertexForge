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
        // True only while the ragdoll's bodies+constraints are added to the Jolt physics system.
        // A ragdoll is built (CreateRagdoll) but NOT added until activate/transition; calling
        // RemoveFromPhysicsSystem() on a not-added ragdoll walks invalid constraint indices and
        // crashes, so every removal site must gate on this flag.
        bool inSystem = false;
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

        // Powered ragdoll: drive joint motors toward the target pose. Per-bone strength
        // scales the motor torque limit; <= ~0.01 turns the joint's motors off entirely.
        void driveRagdollToPose(uint64_t entityId, const JPH::SkeletonPose& targetPose,
                                const std::vector<float>& perBoneStrength,
                                const std::vector<float>& perBoneMaxTorque);
        // Velocity-drive the root body toward the target pose's root joint
        void driveRagdollRoot(uint64_t entityId, const JPH::SkeletonPose& targetPose,
                              float strength, float deltaTime);
        // Turn all joint motors off (powered ragdoll -> passive ragdoll)
        void setRagdollMotorsOff(uint64_t entityId);
        bool isRagdollBelowVelocityThreshold(uint64_t entityId, float linearThreshold,
                                             float angularThreshold) const;

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
        // Clamp every ragdoll body's linear/angular velocity to a finite maximum (and zero out
        // non-finite values) so an applied impulse can't push a body to escape velocity and NaN
        // the broad phase. Called after impulses; no-op unless the ragdoll is in the system.
        void clampRagdollVelocities(uint64_t entityId);

        PhysicsContext* ctx = nullptr;
        PhysicsBodyRegistry* bodyRegistry = nullptr;

        std::unordered_map<uint64_t, RagdollInstanceData> entityRagdolls;
        std::unordered_map<uint64_t, std::vector<JPH::BodyID>> entityBoneBodies;
        uint32_t nextCollisionGroupId = 1;
    };
}
