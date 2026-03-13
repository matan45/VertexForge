#include "PhysicsBodyRegistry.hpp"

namespace core::physics
{
    void PhysicsBodyRegistry::registerBody(uint64_t entityId, JPH::BodyID bodyId)
    {
        entityToBody[entityId] = bodyId;
        bodyToEntity[bodyId.GetIndex()] = entityId;
    }

    void PhysicsBodyRegistry::unregisterBody(JPH::BodyID bodyId)
    {
        auto it = bodyToEntity.find(bodyId.GetIndex());
        if (it != bodyToEntity.end())
        {
            entityToBody.erase(it->second);
            bodyToEntity.erase(it);
        }
    }

    void PhysicsBodyRegistry::unregisterEntity(uint64_t entityId)
    {
        auto it = entityToBody.find(entityId);
        if (it != entityToBody.end())
        {
            bodyToEntity.erase(it->second.GetIndex());
            entityToBody.erase(it);
        }
    }

    JPH::BodyID PhysicsBodyRegistry::getBodyForEntity(uint64_t entityId) const
    {
        auto it = entityToBody.find(entityId);
        return it != entityToBody.end() ? it->second : JPH::BodyID();
    }

    uint64_t PhysicsBodyRegistry::getEntityForBody(JPH::BodyID bodyId) const
    {
        if (bodyId.IsInvalid()) return 0;
        auto it = bodyToEntity.find(bodyId.GetIndex());
        return it != bodyToEntity.end() ? it->second : 0;
    }

    bool PhysicsBodyRegistry::hasEntity(uint64_t entityId) const
    {
        return entityToBody.find(entityId) != entityToBody.end();
    }

    void PhysicsBodyRegistry::clear()
    {
        entityToBody.clear();
        bodyToEntity.clear();
    }

    void PhysicsBodyRegistry::registerBoneIndex(JPH::BodyID bodyId, int boneIndex)
    {
        bodyToBoneIndex[bodyId.GetIndex()] = boneIndex;
    }

    void PhysicsBodyRegistry::unregisterBoneIndex(JPH::BodyID bodyId)
    {
        bodyToBoneIndex.erase(bodyId.GetIndex());
    }

    int PhysicsBodyRegistry::getBoneIndex(JPH::BodyID bodyId) const
    {
        auto it = bodyToBoneIndex.find(bodyId.GetIndex());
        return it != bodyToBoneIndex.end() ? it->second : -1;
    }

    void PhysicsBodyRegistry::clearBoneIndices()
    {
        bodyToBoneIndex.clear();
    }
}
