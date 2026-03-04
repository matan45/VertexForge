#pragma once

namespace services
{
    class IDebugDrawService
    {
    public:
        virtual ~IDebugDrawService() = default;
        virtual void registerEventHandlers() = 0;
    };
}
