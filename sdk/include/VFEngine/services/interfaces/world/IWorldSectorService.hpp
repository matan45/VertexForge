#pragma once

#include "world/WorldTypes.hpp"
#include <glm/glm.hpp>

namespace services
{
    class IWorldSectorService
    {
    public:
        virtual ~IWorldSectorService() = default;

        virtual void registerEventHandlers() = 0;
        virtual void update() = 0;

        virtual bool isWorldMode() const = 0;
    };

} // namespace services
