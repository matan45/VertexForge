#pragma once
#include "../../interfaces/render/IObjectStreamingService.hpp"

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class IObjectStreamingProvider;

    class ObjectStreamingServiceImpl : public IObjectStreamingService
    {
    private:
        IObjectStreamingProvider* provider;

    public:
        explicit ObjectStreamingServiceImpl(IObjectStreamingProvider* provider);
        ~ObjectStreamingServiceImpl() override;

        void registerEventHandlers() override;
    };
}
