#pragma once

namespace services
{
    class IVegetationBrushService
    {
    public:
        virtual ~IVegetationBrushService() = default;
        virtual void registerEventHandlers() = 0;
    };
}
