#pragma once

namespace services
{
    class IBillboardRenderService
    {
    public:
        virtual ~IBillboardRenderService() = default;
        virtual void registerEventHandlers() = 0;
    };
}
