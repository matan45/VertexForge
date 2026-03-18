#pragma once

#include "PhysicsBodyRegistry.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <unordered_map>
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
        void capture(const PhysicsBodyRegistry& registry, JPH::PhysicsSystem& physicsSystem);
        void swap();

        const std::vector<PhysicsBodySnapshot>& getReadBuffer() const { return *readBuf; }
        const std::vector<PhysicsBodySnapshot>& getWriteBuffer() const { return *writeBuf; }

        const PhysicsBodySnapshot* findInReadBuffer(uint64_t entityId) const;
        const PhysicsBodySnapshot* findInWriteBuffer(uint64_t entityId) const;

        void clear();

    private:
        std::vector<PhysicsBodySnapshot> bufferA;
        std::vector<PhysicsBodySnapshot> bufferB;
        std::vector<PhysicsBodySnapshot>* readBuf = &bufferA;
        std::vector<PhysicsBodySnapshot>* writeBuf = &bufferB;

        // Fast entity lookup index (rebuilt on capture)
        std::unordered_map<uint64_t, size_t> readIndex;
        std::unordered_map<uint64_t, size_t> writeIndex;
    };
}
