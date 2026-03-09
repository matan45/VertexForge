#pragma once
#include "../../interfaces/render/ILightStreamingService.hpp"

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class ILightStreamingProvider;

    class LightStreamingServiceImpl : public ILightStreamingService
    {
    private:
        ILightStreamingProvider* provider;

    public:
        explicit LightStreamingServiceImpl(ILightStreamingProvider* provider);
        ~LightStreamingServiceImpl() override;

        void registerEventHandlers() override;
    };
}
