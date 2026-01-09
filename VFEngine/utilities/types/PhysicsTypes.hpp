#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <bitset>
#include <glm/glm.hpp>

namespace types {

    enum class ColliderShape : uint8_t
    {
        Box = 0,
        Sphere = 1,
        Capsule = 2,
        ConvexMesh = 3,
        TriangleMesh = 4
    };

    enum class RigidBodyType : uint8_t
    {
        Static = 0,
        Dynamic = 1,
        Kinematic = 2
    };

    // Collision layer definition
    struct CollisionLayer
    {
        std::string name;
        uint8_t index = 0;      // 0-15
        bool isBuiltIn = false; // Built-in layers cannot be deleted
    };

    // Global physics configuration
    struct PhysicsSettings
    {
        // Maximum number of collision layers supported
        static constexpr uint8_t MAX_LAYERS = 16;

        // Gravity
        glm::vec3 gravity{0.0f, -9.81f, 0.0f};
        float gravityScale = 1.0f;

        // Timestep configuration
        double fixedTimestep = 1.0 / 60.0;  // 60 Hz physics
        double maxAccumulator = 0.25;       // Max 250ms accumulator
        int maxStepsPerFrame = 8;           // Limit steps per frame

        // Sleep thresholds
        float linearSleepThreshold = 0.05f;   // m/s
        float angularSleepThreshold = 0.05f;  // rad/s
        float timeToSleep = 0.5f;             // seconds

        // Collision layers
        std::vector<CollisionLayer> layers;

        // Collision matrix: collisionMatrix[i][j] = true means layer i collides with layer j
        std::array<std::bitset<MAX_LAYERS>, MAX_LAYERS> collisionMatrix;

        // Create default settings with built-in layers
        static PhysicsSettings createDefault()
        {
            PhysicsSettings settings;

            // Built-in layers (matching PhysicsLayers.hpp)
            settings.layers = {
                {"Static", 0, true},
                {"Dynamic", 1, true},
                {"Kinematic", 2, true},
                {"Sensor", 3, true}
            };

            // Initialize collision matrix with default rules
            // Static (0): collides with Dynamic, Kinematic
            settings.collisionMatrix[0].reset();
            settings.collisionMatrix[0].set(1); // Dynamic
            settings.collisionMatrix[0].set(2); // Kinematic

            // Dynamic (1): collides with all
            settings.collisionMatrix[1].reset();
            settings.collisionMatrix[1].set(0); // Static
            settings.collisionMatrix[1].set(1); // Dynamic
            settings.collisionMatrix[1].set(2); // Kinematic
            settings.collisionMatrix[1].set(3); // Sensor

            // Kinematic (2): collides with Static, Dynamic, Sensor (not other Kinematic)
            settings.collisionMatrix[2].reset();
            settings.collisionMatrix[2].set(0); // Static
            settings.collisionMatrix[2].set(1); // Dynamic
            settings.collisionMatrix[2].set(3); // Sensor

            // Sensor (3): collides with Dynamic, Kinematic
            settings.collisionMatrix[3].reset();
            settings.collisionMatrix[3].set(1); // Dynamic
            settings.collisionMatrix[3].set(2); // Kinematic

            return settings;
        }

        // Check if two layers should collide
        bool shouldLayersCollide(uint8_t layer1, uint8_t layer2) const
        {
            if (layer1 >= MAX_LAYERS || layer2 >= MAX_LAYERS) return false;
            return collisionMatrix[layer1].test(layer2);
        }

        // Set collision between two layers (symmetric)
        void setLayerCollision(uint8_t layer1, uint8_t layer2, bool shouldCollide)
        {
            if (layer1 >= MAX_LAYERS || layer2 >= MAX_LAYERS) return;
            collisionMatrix[layer1].set(layer2, shouldCollide);
            collisionMatrix[layer2].set(layer1, shouldCollide);
        }

        // Get layer by index, returns nullptr if not found
        const CollisionLayer* getLayerByIndex(uint8_t index) const
        {
            for (const auto& layer : layers)
            {
                if (layer.index == index) return &layer;
            }
            return nullptr;
        }

        // Get next available layer index (returns MAX_LAYERS if none available)
        uint8_t getNextAvailableLayerIndex() const
        {
            std::bitset<MAX_LAYERS> usedIndices;
            for (const auto& layer : layers)
            {
                usedIndices.set(layer.index);
            }
            for (uint8_t i = 0; i < MAX_LAYERS; ++i)
            {
                if (!usedIndices.test(i)) return i;
            }
            return MAX_LAYERS;
        }
    };

}
