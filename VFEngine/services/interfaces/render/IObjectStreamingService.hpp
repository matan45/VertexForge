#pragma once

namespace services
{
    class IObjectStreamingService
    {
    public:
        virtual ~IObjectStreamingService() = default;
        virtual void registerEventHandlers() = 0;
    };
}
