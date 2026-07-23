#pragma once

namespace services
{
    class IFoliageBrushModeService
    {
    public:
        virtual ~IFoliageBrushModeService() = default;
        virtual void registerEventHandlers() = 0;
    };
}
