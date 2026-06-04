#pragma once

namespace services
{
    class IAssetDatabaseService
    {
    public:
        virtual ~IAssetDatabaseService() = default;
        virtual void registerEventHandlers() = 0;
    };
}
