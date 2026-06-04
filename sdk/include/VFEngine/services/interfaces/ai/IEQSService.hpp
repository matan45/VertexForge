#pragma once

namespace services
{
    class IEQSService
    {
    public:
        virtual ~IEQSService() = default;

        virtual void registerEventHandlers() = 0;
        virtual void update(float frameBudgetMs = 2.0f) = 0;
    };
}
