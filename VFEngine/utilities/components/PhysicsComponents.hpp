#pragma once
#include <glm/glm.hpp>
#include <string>
#include <cstdint>
#include "../types/PhysicsTypes.hpp"
#include "../types/VehicleTypes.hpp"
#include "../asset/AssetRef.hpp"

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
        asset::AssetRef meshRef;
        int32_t submeshIndex = -1;

        bool isTrigger = false;
        uint8_t collisionLayer = 1;

        float friction = 0.5f;
        float restitution = 0.0f;
    };

    // Optional companion to RigidBodyComponent: tunes how the ocean buoyancy loop samples
    // the hull. Entities without it get Auto sampling derived from their collider.
    struct BuoyancyComponent
    {
        enum class SampleMode : uint8_t
        {
            Auto = 0,   // derive sample points from the collider shape
            Custom = 1  // use customPoints (local space)
        };

        SampleMode sampleMode = SampleMode::Auto;
        glm::vec3 customPoints[8]{};
        uint32_t customPointCount = 0;

        float buoyancyScale = 1.0f;
        float angularDrag = 0.5f;
    };

    // VK-1606: an authored source of ripples on the water surface — a bow wave, a propeller wash, a
    // dripping torch. Independent of physics: OceanService derives the speed from how far the entity
    // moved since the last tick, so scripted movers and navmesh agents emit exactly like rigid
    // bodies do. Moving rigid bodies also get an automatic hull wake in updateBuoyancy; this
    // component is for placing extra, offset sources on top of that.
    struct WaterWakeEmitterComponent
    {
        glm::vec3 offset{0.0f};       // local-space offset from the entity origin (e.g. the bow)
        float radius = 1.5f;          // metres of surface the impulse covers
        float strength = 0.5f;        // vertical velocity kick (negative pushes the surface down)
        float minSpeed = 0.5f;        // m/s below which it stays quiet
        float travelInterval = 0.5f;  // metres of travel between impulses
        bool continuous = false;      // emit every tick above minSpeed, ignoring travelInterval
        bool enabled = true;
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

    struct VehicleComponent
    {
        types::VehicleConfig config = types::VehicleConfig::createFourWheelCar();
    };
}
