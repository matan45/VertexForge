#pragma once

namespace services
{
    class IGIService
    {
    public:
        virtual ~IGIService() = default;
        virtual void registerEventHandlers() = 0;
    };
}
