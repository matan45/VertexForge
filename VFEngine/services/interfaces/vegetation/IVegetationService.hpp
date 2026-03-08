#pragma once

namespace services
{
    class IVegetationService
    {
    public:
        virtual ~IVegetationService() = default;
        virtual void registerEventHandlers() = 0;
    };
}
