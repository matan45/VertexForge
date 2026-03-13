#pragma once

namespace services
{
    class IMeshBrushModeService
    {
    public:
        virtual ~IMeshBrushModeService() = default;
        virtual void registerEventHandlers() = 0;
        virtual void activate() = 0;
        virtual void deactivate() = 0;
        virtual bool isActive() const = 0;
    };
}
