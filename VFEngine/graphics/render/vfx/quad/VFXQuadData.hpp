#pragma once

#include "../billboard/VFXBillboardTypes.hpp"
#include <array>
#include <cstdint>

namespace render::vfx
{
    inline constexpr std::array<VFXQuadVertex, 4> QUAD_VERTICES = {{
        {{-0.5f, -0.5f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f}, {0.0f, 0.0f}},
    }};

    inline constexpr std::array<uint16_t, 6> QUAD_INDICES = {0, 1, 2, 2, 3, 0};
}
