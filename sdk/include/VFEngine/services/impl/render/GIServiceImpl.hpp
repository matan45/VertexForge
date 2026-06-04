#pragma once
#include "../../interfaces/render/IGIService.hpp"

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

    public:
        explicit GIServiceImpl(IGIProvider* provider);
        ~GIServiceImpl() override;

        void registerEventHandlers() override;
    };
}
