#pragma once

namespace services
{
    class IWeatherService
    {
    public:
        virtual ~IWeatherService() = default;
        virtual void registerEventHandlers() = 0;
    };
}
