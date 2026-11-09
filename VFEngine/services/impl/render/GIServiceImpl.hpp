#pragma once
#include "../../interfaces/render/IGIService.hpp"
#include "../../events/EventTypes.hpp"

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class IGIProvider;

    class GIServiceImpl : public IGIService
    {
    private:
        IGIProvider* provider;
        ::events::SubscriptionToken renderSettingsChangedToken;

    public:
        explicit GIServiceImpl(IGIProvider* provider);
        ~GIServiceImpl() override;

        void registerEventHandlers() override;
    };
}
