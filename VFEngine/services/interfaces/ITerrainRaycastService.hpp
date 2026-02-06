#pragma once

namespace services
{
    class ITerrainRaycastService
    {
    public:
        virtual ~ITerrainRaycastService() = default;

        virtual void registerEventHandlers() = 0;
    };
}
