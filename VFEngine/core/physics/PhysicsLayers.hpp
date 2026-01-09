#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <array>
#include <bitset>

namespace core::physics {

    // Maximum number of collision layers supported (matches PhysicsSettings::MAX_LAYERS)
    static constexpr uint8_t MAX_COLLISION_LAYERS = 16;

    // Object layers - defines what types of physics objects exist
    // Built-in layers (0-3), user-defined layers (4-15)
    namespace Layers {
        static constexpr JPH::ObjectLayer STATIC = 0;      // Non-moving objects (ground, walls)
        static constexpr JPH::ObjectLayer DYNAMIC = 1;     // Moving objects with physics
        static constexpr JPH::ObjectLayer KINEMATIC = 2;   // Moving objects controlled by game logic
        static constexpr JPH::ObjectLayer SENSOR = 3;      // Trigger volumes (no collision response)
        static constexpr JPH::ObjectLayer NUM_LAYERS = MAX_COLLISION_LAYERS;
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
            // Layer 0 (Static) goes to NON_MOVING broad phase
            // All other layers go to MOVING broad phase
            for (uint8_t i = 0; i < MAX_COLLISION_LAYERS; ++i) {
                objectToBroadPhase[i] = (i == Layers::STATIC) ? BroadPhaseLayers::NON_MOVING : BroadPhaseLayers::MOVING;
            }
        }

        unsigned int GetNumBroadPhaseLayers() const override {
            return BroadPhaseLayers::NUM_LAYERS;
        }

        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
            if (inLayer >= MAX_COLLISION_LAYERS) return BroadPhaseLayers::MOVING;
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
        JPH::BroadPhaseLayer objectToBroadPhase[MAX_COLLISION_LAYERS];
    };

    // Determines if an object layer can collide with a broad-phase layer
    class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
            // Layer 0 (Static) only collides with moving objects
            if (inLayer1 == Layers::STATIC) {
                return inLayer2 == BroadPhaseLayers::MOVING;
            }
            // All other layers can collide with both moving and non-moving
            return true;
        }
    };

    // Dynamic collision matrix - determines which object layers can collide with each other
    // Uses a runtime-configurable collision matrix instead of hardcoded rules
    class DynamicObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
    public:
        DynamicObjectLayerPairFilter() {
            // Initialize with default collision rules
            initializeDefaultMatrix();
        }

        bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const override {
            if (inLayer1 >= MAX_COLLISION_LAYERS || inLayer2 >= MAX_COLLISION_LAYERS) {
                return false;
            }
            return collisionMatrix[inLayer1].test(inLayer2);
        }

        // Set collision between two layers (automatically symmetric)
        void setLayerCollision(uint8_t layer1, uint8_t layer2, bool shouldCollide) {
            if (layer1 >= MAX_COLLISION_LAYERS || layer2 >= MAX_COLLISION_LAYERS) return;
            collisionMatrix[layer1].set(layer2, shouldCollide);
            collisionMatrix[layer2].set(layer1, shouldCollide);
        }

        // Set entire collision matrix from PhysicsSettings
        void setCollisionMatrix(const std::array<std::bitset<MAX_COLLISION_LAYERS>, MAX_COLLISION_LAYERS>& matrix) {
            collisionMatrix = matrix;
        }

        // Get collision matrix for saving
        const std::array<std::bitset<MAX_COLLISION_LAYERS>, MAX_COLLISION_LAYERS>& getCollisionMatrix() const {
            return collisionMatrix;
        }

    private:
        void initializeDefaultMatrix() {
            // Reset all
            for (auto& row : collisionMatrix) {
                row.reset();
            }

            // Static (0): collides with Dynamic, Kinematic
            collisionMatrix[0].set(1);
            collisionMatrix[0].set(2);

            // Dynamic (1): collides with all
            collisionMatrix[1].set(0);
            collisionMatrix[1].set(1);
            collisionMatrix[1].set(2);
            collisionMatrix[1].set(3);

            // Kinematic (2): collides with Static, Dynamic, Sensor (not other Kinematic)
            collisionMatrix[2].set(0);
            collisionMatrix[2].set(1);
            collisionMatrix[2].set(3);

            // Sensor (3): collides with Dynamic, Kinematic
            collisionMatrix[3].set(1);
            collisionMatrix[3].set(2);
        }

        std::array<std::bitset<MAX_COLLISION_LAYERS>, MAX_COLLISION_LAYERS> collisionMatrix;
    };

    // Legacy alias for backward compatibility
    using ObjectLayerPairFilterImpl = DynamicObjectLayerPairFilter;

}
