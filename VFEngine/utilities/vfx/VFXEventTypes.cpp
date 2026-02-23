#include "VFXEventTypes.hpp"

namespace vfx
{
    const char* eventTypeToString(VFXEventType type)
    {
        switch (type)
        {
        case VFXEventType::OnSpawn:             return "OnSpawn";
        case VFXEventType::OnDeath:             return "OnDeath";
        case VFXEventType::OnCollision:         return "OnCollision";
        case VFXEventType::OnLifetimeThreshold: return "OnLifetimeThreshold";
        default: return "OnSpawn";
        }
    }

    VFXEventType stringToEventType(const std::string& str)
    {
        if (str == "OnDeath") return VFXEventType::OnDeath;
        if (str == "OnCollision") return VFXEventType::OnCollision;
        if (str == "OnLifetimeThreshold") return VFXEventType::OnLifetimeThreshold;
        return VFXEventType::OnSpawn;
    }
}
