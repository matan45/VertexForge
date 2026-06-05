#pragma once

#include "../../data/EntityHandle.hpp"
#include <components/DestructionComponents.hpp>
#include <asset/AssetRef.hpp>
#include "../../data/VFXTypes.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace services
{
    struct DestructionEffectsConfig
    {
        uint32_t maxFragmentCollisionSoundsPerSecond = 5;
        float volumeScaleMassFactor = 0.1f;
        float decalHalfExtents = 0.3f;
        float decalLifetime = 15.0f;
        float vfxLifetime = 5.0f;
    };

    struct DestructionEffectsSnapshot
    {
        glm::vec3 position{0.0f};
        float fragmentMassTotal = 1.0f;
        asset::AssetRef onDestroyVFX;
        asset::AssetRef onDestroyAudio;
    };

    class DestructionEffectsManager
    {
    public:
        explicit DestructionEffectsManager(DestructionEffectsConfig config = {});

        void onDamageApplied(EntityHandle entity, float damageAmount,
                             const glm::vec3& impactPoint, const glm::vec3& impactDir,
                             components::DamageType damageType);

        DestructionEffectsSnapshot captureSnapshot(EntityHandle entity);

        void onDestructionTriggered(const DestructionEffectsSnapshot& snapshot,
                                     const glm::vec3& impactPoint, const glm::vec3& impactDir);

        void onFragmentCollision(EntityHandle fragmentEntity,
                                  const glm::vec3& contactPoint, float impulse);

        void update(float deltaTime);

        void reset();

    private:
        DestructionEffectsConfig config;
        float collisionSoundTimer = 0.0f;
        uint32_t collisionSoundsThisSecond = 0;

        struct TimedEntity
        {
            EntityHandle entity;
            float remaining;
        };

        struct TimedVFX
        {
            VFXInstanceId instanceId = 0;
            float remaining;
        };

        std::vector<TimedEntity> timedDecals;
        std::vector<TimedVFX> timedVFXInstances;

        void spawnVFX(const asset::AssetRef& vfxRef, const glm::vec3& position);
        void playSound3D(const asset::AssetRef& audioRef, const glm::vec3& position, float volume);
        void spawnDamageDecal(const asset::AssetRef& albedo, const asset::AssetRef& normal,
                              const glm::vec3& impactPoint, const glm::vec3& impactDir,
                              float halfExtents = 0.3f);
    };
}
