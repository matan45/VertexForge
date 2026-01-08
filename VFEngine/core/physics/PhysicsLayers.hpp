#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

namespace core::physics {

    // Object layers - defines what types of physics objects exist
    namespace Layers {
        static constexpr JPH::ObjectLayer STATIC = 0;      // Non-moving objects (ground, walls)
        static constexpr JPH::ObjectLayer DYNAMIC = 1;     // Moving objects with physics
        static constexpr JPH::ObjectLayer KINEMATIC = 2;   // Moving objects controlled by game logic
        static constexpr JPH::ObjectLayer SENSOR = 3;      // Trigger volumes (no collision response)
        static constexpr JPH::ObjectLayer NUM_LAYERS = 4;
    }

    // Broad-phase layers - for spatial partitioning optimization
    // Groups object layers into fewer categories for efficient broad-phase queries
    namespace BroadPhaseLayers {
        static constexpr JPH::BroadPhaseLayer NON_MOVING{ 0 };
        static constexpr JPH::BroadPhaseLayer MOVING{ 1 };
        static constexpr unsigned int NUM_LAYERS = 2;
    }

    // Maps object layers to broad-phase layers
    class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
    public:
        BroadPhaseLayerInterfaceImpl() {
            // Static objects go to NON_MOVING broad phase
            objectToBroadPhase[Layers::STATIC] = BroadPhaseLayers::NON_MOVING;
            // Dynamic, kinematic, and sensors go to MOVING broad phase
            objectToBroadPhase[Layers::DYNAMIC] = BroadPhaseLayers::MOVING;
            objectToBroadPhase[Layers::KINEMATIC] = BroadPhaseLayers::MOVING;
            objectToBroadPhase[Layers::SENSOR] = BroadPhaseLayers::MOVING;
        }

        unsigned int GetNumBroadPhaseLayers() const override {
            return BroadPhaseLayers::NUM_LAYERS;
        }

        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
            JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
            return objectToBroadPhase[inLayer];
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
            switch (static_cast<JPH::BroadPhaseLayer::Type>(inLayer)) {
            case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::NON_MOVING):
                return "NON_MOVING";
            case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::MOVING):
                return "MOVING";
            default:
                JPH_ASSERT(false);
                return "INVALID";
            }
        }
#endif

    private:
        JPH::BroadPhaseLayer objectToBroadPhase[Layers::NUM_LAYERS];
    };

    // Determines if an object layer can collide with a broad-phase layer
    class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
            switch (inLayer1) {
            case Layers::STATIC:
                // Static objects only collide with moving objects
                return inLayer2 == BroadPhaseLayers::MOVING;
            case Layers::DYNAMIC:
            case Layers::KINEMATIC:
            case Layers::SENSOR:
                // Moving objects can collide with both moving and non-moving
                return true;
            default:
                JPH_ASSERT(false);
                return false;
            }
        }
    };

    // Collision matrix - determines which object layers can collide with each other
    // | Layer     | Static | Dynamic | Kinematic | Sensor |
    // |-----------|--------|---------|-----------|--------|
    // | Static    | No     | Yes     | Yes       | No     |
    // | Dynamic   | Yes    | Yes     | Yes       | Yes    |
    // | Kinematic | Yes    | Yes     | No        | Yes    |
    // | Sensor    | No     | Yes     | Yes       | No     |
    class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const override {
            switch (inLayer1) {
            case Layers::STATIC:
                // Static collides with dynamic and kinematic (not static or sensor)
                return inLayer2 == Layers::DYNAMIC || inLayer2 == Layers::KINEMATIC;

            case Layers::DYNAMIC:
                // Dynamic collides with everything except nothing
                return inLayer2 == Layers::STATIC ||
                    inLayer2 == Layers::DYNAMIC ||
                    inLayer2 == Layers::KINEMATIC ||
                    inLayer2 == Layers::SENSOR;

            case Layers::KINEMATIC:
                // Kinematic collides with static, dynamic, sensor (not other kinematic)
                return inLayer2 == Layers::STATIC ||
                    inLayer2 == Layers::DYNAMIC ||
                    inLayer2 == Layers::SENSOR;

            case Layers::SENSOR:
                // Sensors only detect dynamic and kinematic objects
                return inLayer2 == Layers::DYNAMIC || inLayer2 == Layers::KINEMATIC;

            default:
                JPH_ASSERT(false);
                return false;
            }
        }
    };

}
