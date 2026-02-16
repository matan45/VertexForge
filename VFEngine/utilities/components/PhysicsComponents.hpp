#pragma once
#include <glm/glm.hpp>
#include <string>
#include <cstdint>
#include "../types/PhysicsTypes.hpp"

namespace components
{
    using RigidBodyType = types::RigidBodyType;
    using ColliderShape = types::ColliderShape;

    struct ColliderComponent
    {
        ColliderShape shape = ColliderShape::Box;

        glm::vec3 size{1.0f};
        float height = 2.0f;
        glm::vec3 offset{0.0f};
        std::string meshPath;

        bool isTrigger = false;
        uint8_t collisionLayer = 1;

        float friction = 0.5f;
        float restitution = 0.0f;
    };

    struct RigidBodyComponent
    {
        RigidBodyType type = RigidBodyType::Dynamic;

        float mass = 1.0f;
        float linearDamping = 0.0f;
        float angularDamping = 0.05f;

        bool freezePositionX = false;
        bool freezePositionY = false;
        bool freezePositionZ = false;
        bool freezeRotationX = false;
        bool freezeRotationY = false;
        bool freezeRotationZ = false;
    };
}
