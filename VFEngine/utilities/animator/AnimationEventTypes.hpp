#pragma once
#include <string>
#include <cstdint>

namespace animator
{
    struct AnimationEvent
    {
        std::string name;
        float normalizedTime = 0.0f;
        std::string payload;
    };
}
