#include "PhysicsShapeFactory.hpp"
#include "JoltConversions.hpp"
#include "PhysicsMeshLoader.hpp"
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include "print/Log.hpp"
#include <algorithm>

namespace core::physics
{
    static constexpr float MIN_DIMENSION = 0.001f;

    JPH::Ref<JPH::Shape> PhysicsShapeFactory::makeBoxFallback(const glm::vec3& halfExtents)
    {
        glm::vec3 safeExtents = glm::max(halfExtents, glm::vec3(MIN_DIMENSION));
        float minExtent = std::min({safeExtents.x, safeExtents.y, safeExtents.z});
        float convexRadius = std::min(JPH::cDefaultConvexRadius, minExtent);
        return new JPH::BoxShape(toJolt(safeExtents), convexRadius);
    }

    JPH::Ref<JPH::Shape> PhysicsShapeFactory::createShape(const ColliderCreateInfo& info)
    {
        switch (info.shape)
        {
        case ColliderShape::Box:
            {
                glm::vec3 safeExtents = glm::max(info.halfExtents, glm::vec3(MIN_DIMENSION));
                if (safeExtents != info.halfExtents)
                {
                    vfLogWarning("Box collider half-extents clamped from ({}, {}, {}) to ({}, {}, {})",
                                  info.halfExtents.x, info.halfExtents.y, info.halfExtents.z,
                                  safeExtents.x, safeExtents.y, safeExtents.z);
                }
                float minExtent = std::min({safeExtents.x, safeExtents.y, safeExtents.z});
                float convexRadius = std::min(JPH::cDefaultConvexRadius, minExtent);
                return new JPH::BoxShape(toJolt(safeExtents), convexRadius);
            }

        case ColliderShape::Sphere:
            {
                float safeRadius = std::max(info.radius, MIN_DIMENSION);
                if (safeRadius != info.radius)
                {
                    vfLogWarning("Sphere collider radius clamped from {} to {}", info.radius, safeRadius);
                }
                return new JPH::SphereShape(safeRadius);
            }

        case ColliderShape::Capsule:
            {
                float safeRadius = std::max(info.radius, MIN_DIMENSION);
                float halfHeight = info.height * 0.5f - safeRadius;
                if (halfHeight < MIN_DIMENSION)
                {
                    // Radius exceeds half-height: shrink radius to fit a valid capsule
                    halfHeight = MIN_DIMENSION;
                    safeRadius = std::max(MIN_DIMENSION, info.height * 0.5f - MIN_DIMENSION);
                    vfLogWarning("Capsule collider adjusted: radius {} -> {}, halfHeight {} (from height {})",
                                  info.radius, safeRadius, halfHeight, info.height);
                }
                return new JPH::CapsuleShape(halfHeight, safeRadius);
            }

        case ColliderShape::ConvexMesh:
            return createConvexMeshShape(info);

        case ColliderShape::TriangleMesh:
            return createTriangleMeshShape(info);

        default:
            vfLogWarning("Unknown collider shape type {}, defaulting to unit box", static_cast<int>(info.shape));
            return new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
        }
    }

    std::string PhysicsShapeFactory::makeCacheKey(const std::string& meshPath, ColliderShape shape)
    {
        return meshPath + "#" + std::to_string(static_cast<int>(shape));
    }

    void PhysicsShapeFactory::clearCache()
    {
        std::lock_guard lock(shapeCacheMutex);
        shapeCache.clear();
    }

    JPH::Ref<JPH::Shape> PhysicsShapeFactory::createConvexMeshShape(const ColliderCreateInfo& info)
    {
        if (info.meshPath.empty())
        {
            vfLogWarning("ConvexMesh collider has no mesh path, using box fallback");
            return makeBoxFallback(info.halfExtents);
        }

        {
            std::lock_guard lock(shapeCacheMutex);
            std::string key = makeCacheKey(info.meshPath, info.shape);
            auto it = shapeCache.find(key);
            if (it != shapeCache.end())
            {
                return it->second;
            }
        }

        auto decomposition = PhysicsMeshLoader::loadConvexDecomposition(info.meshPath);
        if (decomposition && !decomposition->hulls.empty())
        {
            JPH::StaticCompoundShapeSettings compoundSettings;

            for (const auto& hull : decomposition->hulls)
            {
                if (hull.vertices.empty())
                    continue;

                JPH::Array<JPH::Vec3> hullVerts;
                hullVerts.reserve(hull.vertices.size());
                for (const auto& v : hull.vertices)
                {
                    hullVerts.push_back(JPH::Vec3(v.x, v.y, v.z));
                }

                JPH::ConvexHullShapeSettings hullSettings(hullVerts.data(), static_cast<int>(hullVerts.size()));
                hullSettings.mMaxConvexRadius = 0.05f;

                auto hullResult = hullSettings.Create();
                if (!hullResult.HasError())
                {
                    compoundSettings.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), hullResult.Get());
                }
            }

            if (compoundSettings.mSubShapes.size() > 0)
            {
                auto result = compoundSettings.Create();
                if (!result.HasError())
                {
                    vfLogInfo("Created ConvexMesh compound collider with {} hulls from: {}",
                               compoundSettings.mSubShapes.size(), info.meshPath);
                    {
                        std::lock_guard lock(shapeCacheMutex);
                        shapeCache[makeCacheKey(info.meshPath, info.shape)] = result.Get();
                    }
                    return result.Get();
                }
            }
        }

        auto meshData = PhysicsMeshLoader::loadAllSubmeshes(info.meshPath, 2);
        if (!meshData || meshData->vertices.empty())
        {
            vfLogWarning("ConvexMesh collider failed to load mesh: {}, using box fallback", info.meshPath);
            return makeBoxFallback(info.halfExtents);
        }

        JPH::Array<JPH::Vec3> joltVertices;
        joltVertices.reserve(meshData->vertices.size());
        for (const auto& v : meshData->vertices)
        {
            joltVertices.push_back(JPH::Vec3(v.x, v.y, v.z));
        }

        JPH::ConvexHullShapeSettings settings(joltVertices.data(), static_cast<int>(joltVertices.size()));
        settings.mMaxConvexRadius = 0.05f;

        auto result = settings.Create();
        if (result.HasError())
        {
            vfLogWarning("ConvexMesh collider creation failed: {}, using box fallback",
                          result.GetError().c_str());
            return makeBoxFallback(info.halfExtents);
        }

        vfLogInfo("Created ConvexMesh collider with {} vertices from: {}",
                   meshData->vertices.size(), info.meshPath);
        {
            std::lock_guard lock(shapeCacheMutex);
            shapeCache[makeCacheKey(info.meshPath, info.shape)] = result.Get();
        }
        return result.Get();
    }

    JPH::Ref<JPH::Shape> PhysicsShapeFactory::createTriangleMeshShape(const ColliderCreateInfo& info)
    {
        if (info.meshPath.empty())
        {
            vfLogWarning("TriangleMesh collider has no mesh path, using box fallback");
            return makeBoxFallback(info.halfExtents);
        }

        {
            std::lock_guard lock(shapeCacheMutex);
            std::string key = makeCacheKey(info.meshPath, info.shape);
            auto it = shapeCache.find(key);
            if (it != shapeCache.end())
            {
                return it->second;
            }
        }

        auto meshData = PhysicsMeshLoader::loadAllSubmeshes(info.meshPath, 2);
        if (!meshData || meshData->vertices.empty() || meshData->indices.empty())
        {
            vfLogWarning("TriangleMesh collider failed to load mesh: {}, using box fallback", info.meshPath);
            return makeBoxFallback(info.halfExtents);
        }

        JPH::TriangleList triangles;
        triangles.reserve(meshData->indices.size() / 3);

        for (size_t i = 0; i + 2 < meshData->indices.size(); i += 3)
        {
            const auto& v0 = meshData->vertices[meshData->indices[i]];
            const auto& v1 = meshData->vertices[meshData->indices[i + 1]];
            const auto& v2 = meshData->vertices[meshData->indices[i + 2]];

            triangles.push_back(JPH::Triangle(
                JPH::Float3(v0.x, v0.y, v0.z),
                JPH::Float3(v1.x, v1.y, v1.z),
                JPH::Float3(v2.x, v2.y, v2.z)
            ));
        }

        JPH::MeshShapeSettings settings(triangles);

        auto result = settings.Create();
        if (result.HasError())
        {
            vfLogWarning("TriangleMesh collider creation failed: {}, using box fallback",
                          result.GetError().c_str());
            return makeBoxFallback(info.halfExtents);
        }

        vfLogInfo("Created TriangleMesh collider with {} triangles from: {}",
                   triangles.size(), info.meshPath);
        {
            std::lock_guard lock(shapeCacheMutex);
            shapeCache[makeCacheKey(info.meshPath, info.shape)] = result.Get();
        }
        return result.Get();
    }

    JPH::EMotionType PhysicsShapeFactory::getMotionType(BodyType type)
    {
        switch (type)
        {
        case BodyType::Static:
            return JPH::EMotionType::Static;
        case BodyType::Dynamic:
            return JPH::EMotionType::Dynamic;
        case BodyType::Kinematic:
            return JPH::EMotionType::Kinematic;
        default:
            return JPH::EMotionType::Dynamic;
        }
    }
}
