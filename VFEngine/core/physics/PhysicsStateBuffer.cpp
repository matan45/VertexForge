#include "PhysicsStateBuffer.hpp"
#include "JoltConversions.hpp"
#include <Jolt/Physics/Body/BodyInterface.h>

namespace core::physics
{
    void PhysicsStateBuffer::capture(const PhysicsBodyRegistry& registry,
                                      JPH::PhysicsSystem& physicsSystem)
    {
        const auto& entityBodies = registry.getAllEntityBodies();
        auto& bodyInterface = physicsSystem.GetBodyInterfaceNoLock();

        writeBuf->clear();
        writeBuf->reserve(entityBodies.size());
        writeIndex.clear();
        writeIndex.reserve(entityBodies.size());

        size_t idx = 0;
        for (const auto& [entityId, bodyId] : entityBodies)
        {
            if (bodyId.IsInvalid()) continue;

            PhysicsBodySnapshot snapshot;
            snapshot.entityId = entityId;

            JPH::RVec3 pos;
            JPH::Quat rot;
            bodyInterface.GetPositionAndRotation(bodyId, pos, rot);

            snapshot.position = toGlmR(pos);
            snapshot.rotation = toGlm(rot);
            snapshot.linearVelocity = toGlm(bodyInterface.GetLinearVelocity(bodyId));

            writeBuf->push_back(snapshot);
            writeIndex[entityId] = idx++;
        }
    }

    void PhysicsStateBuffer::swap()
    {
        std::swap(readBuf, writeBuf);
        std::swap(readIndex, writeIndex);
    }

    const PhysicsBodySnapshot* PhysicsStateBuffer::findInReadBuffer(uint64_t entityId) const
    {
        auto it = readIndex.find(entityId);
        if (it == readIndex.end()) return nullptr;
        return &(*readBuf)[it->second];
    }

    const PhysicsBodySnapshot* PhysicsStateBuffer::findInWriteBuffer(uint64_t entityId) const
    {
        auto it = writeIndex.find(entityId);
        if (it == writeIndex.end()) return nullptr;
        return &(*writeBuf)[it->second];
    }

    void PhysicsStateBuffer::clear()
    {
        bufferA.clear();
        bufferB.clear();
        readIndex.clear();
        writeIndex.clear();
    }
}
