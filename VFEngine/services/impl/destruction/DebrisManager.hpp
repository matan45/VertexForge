#pragma once

#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <destruction/FragmentPool.hpp>
#include <asset/AssetRef.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <deque>
#include <cstdint>

namespace services
{
    struct DebrisConfig
    {
        uint32_t maxActiveFragments = 500;
        uint32_t maxSpawnsPerFrame = 5;
        float defaultLifetime = 10.0f;
        float fadeOutDuration = 1.5f;
        uint32_t maxSpawnQueueSize = 200;
    };

    struct FragmentSpawnRequest
    {
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};
        glm::vec3 scale{1.0f};
        asset::AssetRef fractureAssetRef;
        MaterialData sourceMaterial;
        float mass = 1.0f;
        float lifetime = 10.0f;
        glm::vec3 impulse{0.0f};
        uint64_t sourceEntityId = ~0ULL;
        uint32_t fragmentIndex = 0;
        components::MaterialType materialType = components::MaterialType::Default;
        asset::AssetRef collisionAudioRef;
    };

    class DebrisManager
    {
    public:
        explicit DebrisManager(DebrisConfig config = {});

        void requestSpawn(std::vector<FragmentSpawnRequest> fragments);
        void update(float deltaTime, uint32_t frameNumber);
        void reset();
        uint32_t getActiveCount() const;

    private:
        DebrisConfig config;
        destruction::FragmentPool pool;
        std::deque<FragmentSpawnRequest> spawnQueue;
        uint32_t activeCount = 0;

        void processPendingSpawns(uint32_t frameNumber);
        void updateLifecycles(float deltaTime);
        void enforceBudget();
        void updateFadeOuts(float deltaTime);
        void spawnSingleFragment(const FragmentSpawnRequest& request, uint32_t frameNumber);
    };
}
