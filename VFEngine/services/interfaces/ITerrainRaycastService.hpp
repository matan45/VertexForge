#pragma once

#include "terrain/TerrainHitResult.hpp"

namespace services
{
    class ITerrainRaycastService
    {
    public:
        virtual ~ITerrainRaycastService() = default;

        virtual void registerEventHandlers() = 0;
    };
}
