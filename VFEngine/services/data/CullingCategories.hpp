#pragma once
#include <cstdint>

namespace services
{
    namespace CullingCategory
    {
        constexpr uint32_t StaticMesh = 0;
        constexpr uint32_t Terrain = 1;
        constexpr uint32_t Foliage = 2;
        constexpr uint32_t VFX = 3;
        constexpr uint32_t Decals = 4;
        constexpr uint32_t Billboard = 5;
        constexpr uint32_t Water = 6;
        constexpr uint32_t Count = 7;
    }
}
