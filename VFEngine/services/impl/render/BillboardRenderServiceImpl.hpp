#pragma once
#include "../../interfaces/render/IBillboardRenderService.hpp"

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class IBillboardRenderProvider;

    class BillboardRenderServiceImpl : public IBillboardRenderService
    {
    private:
        IBillboardRenderProvider* provider;

    public:
        explicit BillboardRenderServiceImpl(IBillboardRenderProvider* provider);
        ~BillboardRenderServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void registerBillboardHandlers(::events::EventDispatcher& dispatcher);
    };
}
