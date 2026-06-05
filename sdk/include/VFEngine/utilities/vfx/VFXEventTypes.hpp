#pragma once

#include <string>
#include <cstdint>

namespace vfx
{
    enum class VFXEventType : uint8_t
    {
        OnSpawn = 0,
        OnDeath = 1,
        OnCollision = 2,
        OnLifetimeThreshold = 3
    };

    inline constexpr uint32_t VFX_EVENT_TYPE_COUNT = 4;

    namespace EventFlags
    {
        inline constexpr uint32_t OnSpawn = 1 << 0;
        inline constexpr uint32_t OnDeath = 1 << 1;
        inline constexpr uint32_t OnCollision = 1 << 2;
        inline constexpr uint32_t OnLifetimeThreshold = 1 << 3;
    }

    struct VFXEventConfig
    {
        bool onSpawnEnabled = false;
        std::string onSpawnVFXPath;

        bool onDeathEnabled = false;
        std::string onDeathVFXPath;

        bool onCollisionEnabled = false;
        std::string onCollisionVFXPath;

        bool onLifetimeThresholdEnabled = false;
        std::string onLifetimeThresholdVFXPath;
        float lifetimeThreshold = 0.5f;

        uint32_t toEventFlags() const
        {
            uint32_t flags = 0;
            if (onSpawnEnabled) flags |= EventFlags::OnSpawn;
            if (onDeathEnabled) flags |= EventFlags::OnDeath;
            if (onCollisionEnabled) flags |= EventFlags::OnCollision;
            if (onLifetimeThresholdEnabled) flags |= EventFlags::OnLifetimeThreshold;
            return flags;
        }
    };

    const char* eventTypeToString(VFXEventType type);
    VFXEventType stringToEventType(const std::string& str);

    namespace EventDefaults
    {
        inline constexpr float LIFETIME_THRESHOLD = 0.5f;
    }
}
