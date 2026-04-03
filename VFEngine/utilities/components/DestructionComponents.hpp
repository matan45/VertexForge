#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include "../asset/AssetRef.hpp"

namespace components
{
    enum class DestructionMode : uint8_t
    {
        OneShot,
        Progressive
    };

    enum class DamageType : uint8_t
    {
        Any = 0,
        Explosive = 1,
        Ballistic = 2,
        Melee = 3
    };

    enum class FragmentState : uint8_t
    {
        Active,
        Sleeping,
        FadingOut,
        Pooled
    };

    struct DestructibleComponent
    {
        float maxHealth = 100.0f;
        float currentHealth = 100.0f;
        float destructionThreshold = 0.0f;
        asset::AssetRef fractureAssetRef;
        DestructionMode mode = DestructionMode::OneShot;
        DamageType damageFilter = DamageType::Any;
        float fragmentMassTotal = 1.0f;
        float fragmentLifetime = 10.0f;
        bool isDestroyed = false;

        float propagationRadius = 0.0f;
        float propagationDamage = 50.0f;

        asset::AssetRef onDamageVFX;
        asset::AssetRef onDestroyVFX;
        asset::AssetRef onDamageAudio;
        asset::AssetRef onDestroyAudio;
        asset::AssetRef fragmentCollisionAudio;
        asset::AssetRef damageDecalAlbedo;
        asset::AssetRef damageDecalNormal;
    };

    struct FragmentComponent
    {
        uint64_t sourceEntityId = ~0ULL;
        uint32_t fragmentIndex = 0;
        float lifetime = 10.0f;
        float elapsed = 0.0f;
        FragmentState state = FragmentState::Active;
        float sleepTime = 0.0f;
        float fadeOutDuration = 1.5f;
        float fadeProgress = 0.0f;
        float distanceToCamera = 0.0f;
        uint32_t spawnFrame = 0;

        asset::AssetRef collisionAudioRef;
    };
}
