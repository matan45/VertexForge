#pragma once

namespace services
{
    class IFoliageBrushService
    {
    public:
        virtual ~IFoliageBrushService() = default;
        virtual void registerEventHandlers() = 0;
    };
}
