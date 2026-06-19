#pragma once
#include "../../interfaces/render/IPostProcessEffectService.hpp"

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class IPostProcessEffectProvider;

    class PostProcessEffectServiceImpl : public IPostProcessEffectService
    {
    private:
        IPostProcessEffectProvider* provider;

    public:
        explicit PostProcessEffectServiceImpl(IPostProcessEffectProvider* provider);
        ~PostProcessEffectServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void registerEffectHandlers(::events::EventDispatcher& dispatcher);
    };
}
