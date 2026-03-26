#pragma once

namespace services
{
    class IEQSService
    {
    public:
        virtual ~IEQSService() = default;

        virtual void registerEventHandlers() = 0;
    };
}
