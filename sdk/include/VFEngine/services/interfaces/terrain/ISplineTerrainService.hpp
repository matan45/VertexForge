#pragma once

namespace services
{
    class ISplineTerrainService
    {
    public:
        virtual ~ISplineTerrainService() = default;

        virtual void registerEventHandlers() = 0;
    };
}
