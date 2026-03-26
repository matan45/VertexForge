#pragma once

namespace services
{
    class IVolumetricNavService
    {
    public:
        virtual ~IVolumetricNavService() = default;
        virtual void registerEventHandlers() = 0;
    };
}
