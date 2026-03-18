#pragma once

#include "PhysicsBodyRegistry.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <cstdint>

namespace core::physics
{
    struct PhysicsBodySnapshot
    {
        uint64_t entityId = 0;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 linearVelocity{0.0f};
    };

    class PhysicsStateBuffer
    {
    public:
        // Called on worker thread after physics step completes
        void capture(const PhysicsBodyRegistry& registry, JPH::PhysicsSystem& physicsSystem);

        // Called on main thread before kicking async step
        void swap();

        const PhysicsBodySnapshot* findInReadBuffer(uint64_t entityId) const;
        const PhysicsBodySnapshot* findInWriteBuffer(uint64_t entityId) const;

        void clear();

    private:
        struct Buffer
        {
            std::vector<PhysicsBodySnapshot> snapshots;
            std::unordered_map<uint64_t, size_t> index;
        };

        Buffer buffers[2];
        std::atomic<int> readIdx{0};  // Atomic index for thread-safe buffer identification

        Buffer& readBuffer() { return buffers[readIdx.load(std::memory_order_acquire)]; }
        const Buffer& readBuffer() const { return buffers[readIdx.load(std::memory_order_acquire)]; }
        Buffer& writeBuffer() { return buffers[1 - readIdx.load(std::memory_order_acquire)]; }
        const Buffer& writeBuffer() const { return buffers[1 - readIdx.load(std::memory_order_acquire)]; }
    };
}
