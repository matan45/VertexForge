#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include "PhysicsWorld.hpp"
#include <unordered_map>
#include <string>
#include <mutex>

namespace core::physics
{
    class PhysicsShapeFactory
    {
    public:
        static JPH::Ref<JPH::Shape> createShape(const ColliderCreateInfo& info);
        static JPH::EMotionType getMotionType(BodyType type);
        static void clearCache();
        static void evictFromCache(const std::string& meshPath);

    private:
        static JPH::Ref<JPH::Shape> makeBoxFallback(const glm::vec3& halfExtents);
        static JPH::Ref<JPH::Shape> createConvexMeshShape(const ColliderCreateInfo& info);
        static JPH::Ref<JPH::Shape> createTriangleMeshShape(const ColliderCreateInfo& info);

        static std::string makeCacheKey(const std::string& meshPath, ColliderShape shape);

        inline static std::unordered_map<std::string, JPH::Ref<JPH::Shape>> shapeCache;
        inline static std::mutex shapeCacheMutex;
    };
}
