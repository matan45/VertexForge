#pragma once

namespace services
{
    class IDecalRenderService
    {
    public:
        virtual ~IDecalRenderService() = default;
        virtual void registerEventHandlers() = 0;
    };
}
