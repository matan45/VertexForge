#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <unordered_map>
#include <cstdint>

namespace core::physics
{
    class PhysicsBodyRegistry
    {
    public:
        void registerBody(uint64_t entityId, JPH::BodyID bodyId);
        void unregisterBody(JPH::BodyID bodyId);
        void unregisterEntity(uint64_t entityId);

        // Register/clear ONLY the body->entity reverse link (for collision-callback lookups),
        // WITHOUT claiming the entity's single main-rigid-body slot (entityToBody). Ragdoll and
        // kinematic-bone bodies use these — there are many per entity, so they must not overwrite
        // the Collider/RigidBody body that owns entityToBody via registerBody(). Mixing them was a
        // crash: a destroyed bone body left a stale entityToBody entry that removeRigidBodyByEntity
        // / cleanUp then double-destroyed.
        void registerBodyEntityLink(uint64_t entityId, JPH::BodyID bodyId);
        void unregisterBodyEntityLink(JPH::BodyID bodyId);
        JPH::BodyID getBodyForEntity(uint64_t entityId) const;
        uint64_t getEntityForBody(JPH::BodyID bodyId) const;
        bool hasEntity(uint64_t entityId) const;
        void clear();
        const std::unordered_map<uint64_t, JPH::BodyID>& getAllEntityBodies() const { return entityToBody; }

        void registerBoneIndex(JPH::BodyID bodyId, int boneIndex);
        void unregisterBoneIndex(JPH::BodyID bodyId);
        int getBoneIndex(JPH::BodyID bodyId) const;
        void clearBoneIndices();

    private:
        std::unordered_map<uint64_t, JPH::BodyID> entityToBody;
        std::unordered_map<uint32_t, uint64_t> bodyToEntity;
        std::unordered_map<uint32_t, int> bodyToBoneIndex;
    };
}
