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

        auto& buf = writeBuffer();
        buf.snapshots.clear();
        buf.snapshots.reserve(entityBodies.size());
        buf.index.clear();
        buf.index.reserve(entityBodies.size());

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

            buf.snapshots.push_back(snapshot);
            buf.index[entityId] = idx++;
        }
    }

    void PhysicsStateBuffer::swap()
    {
        int current = readIdx.load(std::memory_order_acquire);
        readIdx.store(1 - current, std::memory_order_release);
    }

    const PhysicsBodySnapshot* PhysicsStateBuffer::findInReadBuffer(uint64_t entityId) const
    {
        const auto& buf = readBuffer();
        auto it = buf.index.find(entityId);
        if (it == buf.index.end()) return nullptr;
        return &buf.snapshots[it->second];
    }

    const PhysicsBodySnapshot* PhysicsStateBuffer::findInWriteBuffer(uint64_t entityId) const
    {
        const auto& buf = writeBuffer();
        auto it = buf.index.find(entityId);
        if (it == buf.index.end()) return nullptr;
        return &buf.snapshots[it->second];
    }

    void PhysicsStateBuffer::clear()
    {
        buffers[0].snapshots.clear();
        buffers[0].index.clear();
        buffers[1].snapshots.clear();
        buffers[1].index.clear();
    }
}
