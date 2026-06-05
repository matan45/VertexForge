#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <bitset>
#include <glm/glm.hpp>

namespace types
{
    enum class ColliderShape : uint8_t
    {
        Box = 0,
        Sphere = 1,
        Capsule = 2,
        ConvexMesh = 3,
        TriangleMesh = 4,
        HeightField = 5
    };

    enum class RigidBodyType : uint8_t
    {
        Static = 0,
        Dynamic = 1,
        Kinematic = 2
    };

    struct CollisionLayer
    {
        std::string name;
        uint8_t index = 0; // 0-15
        bool isBuiltIn = false; // Built-in layers cannot be deleted
    };

    struct PhysicsSettings
    {
        static constexpr uint8_t MAX_LAYERS = 16;

    private:
        mutable std::array<int8_t, MAX_LAYERS> layerIndexCache;
        mutable bool cacheValid = false;

        void rebuildCache() const
        {
            layerIndexCache.fill(-1);
            for (size_t i = 0; i < layers.size(); ++i)
            {
                if (layers[i].index < MAX_LAYERS)
                {
                    layerIndexCache[layers[i].index] = static_cast<int8_t>(i);
                }
            }
            cacheValid = true;
        }

    public:
        PhysicsSettings() = default;

        PhysicsSettings(const PhysicsSettings& other)
            : gravity(other.gravity), gravityScale(other.gravityScale)
              , fixedTimestep(other.fixedTimestep), maxAccumulator(other.maxAccumulator)
              , maxStepsPerFrame(other.maxStepsPerFrame)
              , linearSleepThreshold(other.linearSleepThreshold)
              , angularSleepThreshold(other.angularSleepThreshold)
              , timeToSleep(other.timeToSleep)
              , maxVFXSceneColliders(other.maxVFXSceneColliders)
              , layers(other.layers), collisionMatrix(other.collisionMatrix)
        {
        } // Invalidate cache on copy

        PhysicsSettings& operator=(const PhysicsSettings& other)
        {
            if (this != &other)
            {
                gravity = other.gravity;
                gravityScale = other.gravityScale;
                fixedTimestep = other.fixedTimestep;
                maxAccumulator = other.maxAccumulator;
                maxStepsPerFrame = other.maxStepsPerFrame;
                linearSleepThreshold = other.linearSleepThreshold;
                angularSleepThreshold = other.angularSleepThreshold;
                timeToSleep = other.timeToSleep;
                maxVFXSceneColliders = other.maxVFXSceneColliders;
                layers = other.layers;
                collisionMatrix = other.collisionMatrix;
                cacheValid = false; // Invalidate cache on assignment
            }
            return *this;
        }

        PhysicsSettings(PhysicsSettings&& other) noexcept = default;

        PhysicsSettings& operator=(PhysicsSettings&& other) noexcept
        {
            if (this != &other)
            {
                gravity = other.gravity;
                gravityScale = other.gravityScale;
                fixedTimestep = other.fixedTimestep;
                maxAccumulator = other.maxAccumulator;
                maxStepsPerFrame = other.maxStepsPerFrame;
                linearSleepThreshold = other.linearSleepThreshold;
                angularSleepThreshold = other.angularSleepThreshold;
                timeToSleep = other.timeToSleep;
                maxVFXSceneColliders = other.maxVFXSceneColliders;
                layers = std::move(other.layers);
                collisionMatrix = std::move(other.collisionMatrix);
                cacheValid = false; // Invalidate cache on move assignment
            }
            return *this;
        }

        glm::vec3 gravity{0.0f, -9.81f, 0.0f};
        float gravityScale = 1.0f;

        double fixedTimestep = 1.0 / 60.0; // 60 Hz
        double maxAccumulator = 0.25; // 250ms max
        int maxStepsPerFrame = 8;

        float linearSleepThreshold = 0.05f; // m/s
        float angularSleepThreshold = 0.05f; // rad/s
        float timeToSleep = 0.5f; // seconds

        uint32_t maxVFXSceneColliders = 32; // 1-128

        std::vector<CollisionLayer> layers;

        std::array<std::bitset<MAX_LAYERS>, MAX_LAYERS> collisionMatrix;

        static PhysicsSettings createDefault()
        {
            PhysicsSettings settings;

            settings.layers = {
                {"Static", 0, true},
                {"Dynamic", 1, true},
                {"Kinematic", 2, true},
                {"Sensor", 3, true}
            };

            settings.collisionMatrix[0].reset();
            settings.collisionMatrix[0].set(1); // Dynamic
            settings.collisionMatrix[0].set(2); // Kinematic

            settings.collisionMatrix[1].reset();
            settings.collisionMatrix[1].set(0); // Static
            settings.collisionMatrix[1].set(1); // Dynamic
            settings.collisionMatrix[1].set(2); // Kinematic
            settings.collisionMatrix[1].set(3); // Sensor

            settings.collisionMatrix[2].reset();
            settings.collisionMatrix[2].set(0); // Static
            settings.collisionMatrix[2].set(1); // Dynamic
            settings.collisionMatrix[2].set(3); // Sensor

            settings.collisionMatrix[3].reset();
            settings.collisionMatrix[3].set(1); // Dynamic
            settings.collisionMatrix[3].set(2); // Kinematic

            return settings;
        }

        bool shouldLayersCollide(uint8_t layer1, uint8_t layer2) const
        {
            if (layer1 >= MAX_LAYERS || layer2 >= MAX_LAYERS) return false;
            return collisionMatrix[layer1].test(layer2);
        }

        void setLayerCollision(uint8_t layer1, uint8_t layer2, bool shouldCollide)
        {
            if (layer1 >= MAX_LAYERS || layer2 >= MAX_LAYERS) return;
            collisionMatrix[layer1].set(layer2, shouldCollide);
            collisionMatrix[layer2].set(layer1, shouldCollide);
        }

        const CollisionLayer* getLayerByIndex(uint8_t index) const
        {
            if (index >= MAX_LAYERS) return nullptr;

            if (!cacheValid) rebuildCache();

            int8_t pos = layerIndexCache[index];
            if (pos < 0 || static_cast<size_t>(pos) >= layers.size()) return nullptr;
            return &layers[static_cast<size_t>(pos)];
        }

        const CollisionLayer* getLayerByName(const std::string& name) const
        {
            for (const auto& layer : layers)
            {
                if (layer.name == name) return &layer;
            }
            return nullptr;
        }

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
