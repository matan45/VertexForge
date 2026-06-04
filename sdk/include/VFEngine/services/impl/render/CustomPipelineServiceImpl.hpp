#pragma once
#include "../../interfaces/render/ICustomPipelineService.hpp"

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class ICustomPipelineProvider;

    class CustomPipelineServiceImpl : public ICustomPipelineService
    {
    private:
        ICustomPipelineProvider* provider;

    public:
        explicit CustomPipelineServiceImpl(ICustomPipelineProvider* provider);
        ~CustomPipelineServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void registerPipelineHandlers(::events::EventDispatcher& dispatcher);
    };
}
