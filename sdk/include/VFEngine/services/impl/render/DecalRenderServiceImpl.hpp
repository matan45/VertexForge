#pragma once
#include "../../interfaces/render/IDecalRenderService.hpp"

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class IDecalRenderProvider;

    class DecalRenderServiceImpl : public IDecalRenderService
    {
    private:
        IDecalRenderProvider* provider;

    public:
        explicit DecalRenderServiceImpl(IDecalRenderProvider* provider);
        ~DecalRenderServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void registerDecalHandlers(::events::EventDispatcher& dispatcher);
    };
}
