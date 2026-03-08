#pragma once

#include <cstdint>

namespace render::gpudriven
{
    struct CrossfadeConfig
    {
        float transitionWidth = 10.0f;  // World-space width of the crossfade zone
        bool enabled = false;
    };
}
