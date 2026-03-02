#pragma once
#include "../interfaces/IRenderHookService.hpp"

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class IRenderHookProvider;

    class RenderHookServiceImpl : public IRenderHookService
    {
    private:
        IRenderHookProvider* provider;

    public:
        explicit RenderHookServiceImpl(IRenderHookProvider* provider);
        ~RenderHookServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void registerHookHandlers(::events::EventDispatcher& dispatcher);
    };
}
