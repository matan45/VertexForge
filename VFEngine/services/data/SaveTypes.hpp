#pragma once
#include <string>
#include <vector>

namespace services
{
    struct SaveSlotMetadata
    {
        std::string slotName;
        std::string timestamp;
        float playtimeSeconds = 0.0f;
        std::string customData;
    };
}
