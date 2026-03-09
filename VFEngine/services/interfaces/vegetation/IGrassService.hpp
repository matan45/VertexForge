#pragma once

namespace services
{
    class IGrassService
    {
    public:
        virtual ~IGrassService() = default;
        virtual void registerEventHandlers() = 0;
    };
}
