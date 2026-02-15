#pragma once

#include "../../services/providers/IPhysicsProvider.hpp"
#include "../physics/PhysicsWorld.hpp"

// Compile-time validation that enum values match between physics and services layers
static_assert(
    static_cast<int>(core::physics::BodyType::Static) ==
    static_cast<int>(services::RigidBodyData::Type::Static),
    "BodyType::Static mismatch between core::physics and services");
static_assert(
    static_cast<int>(core::physics::BodyType::Dynamic) ==
    static_cast<int>(services::RigidBodyData::Type::Dynamic),
    "BodyType::Dynamic mismatch between core::physics and services");
static_assert(
    static_cast<int>(core::physics::BodyType::Kinematic) ==
    static_cast<int>(services::RigidBodyData::Type::Kinematic),
    "BodyType::Kinematic mismatch between core::physics and services");

static_assert(
    static_cast<int>(core::physics::ColliderShape::Box) ==
    static_cast<int>(services::ColliderData::Shape::Box),
    "ColliderShape::Box mismatch between core::physics and services");
static_assert(
    static_cast<int>(core::physics::ColliderShape::Sphere) ==
    static_cast<int>(services::ColliderData::Shape::Sphere),
    "ColliderShape::Sphere mismatch between core::physics and services");
static_assert(
    static_cast<int>(core::physics::ColliderShape::Capsule) ==
    static_cast<int>(services::ColliderData::Shape::Capsule),
    "ColliderShape::Capsule mismatch between core::physics and services");
static_assert(
    static_cast<int>(core::physics::ColliderShape::ConvexMesh) ==
    static_cast<int>(services::ColliderData::Shape::ConvexMesh),
    "ColliderShape::ConvexMesh mismatch between core::physics and services");
static_assert(
    static_cast<int>(core::physics::ColliderShape::TriangleMesh) ==
    static_cast<int>(services::ColliderData::Shape::TriangleMesh),
    "ColliderShape::TriangleMesh mismatch between core::physics and services");

namespace core
{
    inline physics::RigidBodyCreateInfo toPhysicsBodyInfo(const services::RigidBodyData& data)
    {
        physics::RigidBodyCreateInfo info;

        switch (data.type)
        {
        case services::RigidBodyData::Type::Static:
            info.type = physics::BodyType::Static;
            break;
        case services::RigidBodyData::Type::Dynamic:
            info.type = physics::BodyType::Dynamic;
            break;
        case services::RigidBodyData::Type::Kinematic:
            info.type = physics::BodyType::Kinematic;
            break;
        }

        info.mass = data.mass;
        info.linearDamping = data.linearDamping;
        info.angularDamping = data.angularDamping;
        info.linearVelocity = data.linearVelocity;
        info.angularVelocity = data.angularVelocity;

        return info;
    }

    inline physics::ColliderCreateInfo toPhysicsColliderInfo(const services::ColliderData& data)
    {
        physics::ColliderCreateInfo info;

        switch (data.shape)
        {
        case services::ColliderData::Shape::Box:
            info.shape = physics::ColliderShape::Box;
            info.halfExtents = data.size * 0.5f;
            break;
        case services::ColliderData::Shape::Sphere:
            info.shape = physics::ColliderShape::Sphere;
            info.radius = data.size.x;
            break;
        case services::ColliderData::Shape::Capsule:
            info.shape = physics::ColliderShape::Capsule;
            info.radius = data.size.x;
            info.height = data.height;
            break;
        case services::ColliderData::Shape::ConvexMesh:
            info.shape = physics::ColliderShape::ConvexMesh;
            info.halfExtents = data.size * 0.5f;
            info.meshPath = data.meshPath;
            break;
        case services::ColliderData::Shape::TriangleMesh:
            info.shape = physics::ColliderShape::TriangleMesh;
            info.halfExtents = data.size * 0.5f;
            info.meshPath = data.meshPath;
            break;
        }

        info.offset = data.offset;
        info.isTrigger = data.isTrigger;
        info.collisionLayer = data.collisionLayer;

        return info;
    }

    inline physics::TerrainHeightFieldCreateInfo toTerrainCreateInfo(
        const services::TerrainTileColliderInfo& tile)
    {
        physics::TerrainHeightFieldCreateInfo info;
        info.heightSamples = tile.heightSamples;
        info.sampleCount = tile.sampleCount;
        info.offset = glm::vec3(tile.worldOrigin.x, 0.0f, tile.worldOrigin.z);
        info.scale = glm::vec3(tile.vertexSpacing, 1.0f, tile.vertexSpacing);
        info.friction = tile.friction;
        info.restitution = tile.restitution;
        info.collisionLayer = tile.collisionLayer;
        return info;
    }
}
