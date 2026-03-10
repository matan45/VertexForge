#pragma once

namespace services
{
    class IMeshBrushService
    {
    public:
        virtual ~IMeshBrushService() = default;
        virtual void registerEventHandlers() = 0;
    };
}
