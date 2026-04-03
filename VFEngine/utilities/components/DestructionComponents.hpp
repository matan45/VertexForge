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

    struct DestructibleComponent
    {
        float maxHealth = 100.0f;
        float currentHealth = 100.0f;
        float destructionThreshold = 0.0f;
        asset::AssetRef fractureAssetRef;
        DestructionMode mode = DestructionMode::OneShot;
        DamageType damageFilter = DamageType::Any;
        float fragmentMassTotal = 1.0f;
        float fragmentLifetime = 5.0f;
        bool isDestroyed = false;
    };

    struct FragmentComponent
    {
        uint64_t sourceEntityId = ~0ULL;
        uint32_t fragmentIndex = 0;
        float lifetime = 5.0f;
        float elapsed = 0.0f;
    };
}
