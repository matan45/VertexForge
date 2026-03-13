#pragma once

namespace services
{
    class ILightStreamingService
    {
    public:
        virtual ~ILightStreamingService() = default;
        virtual void registerEventHandlers() = 0;
    };
}
