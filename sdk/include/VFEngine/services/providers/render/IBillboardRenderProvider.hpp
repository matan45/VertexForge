#pragma once

#include <cstdint>

namespace services
{
    struct BillboardRenderStats
    {
        uint32_t totalInstances = 0;
        uint32_t visibleInstances = 0;
        uint32_t culledByFrustum = 0;
        uint32_t culledByDistance = 0;
        bool renderingEnabled = true;
    };

    class IBillboardRenderProvider
    {
    public:
        virtual ~IBillboardRenderProvider() = default;

        virtual void setBillboardRenderingEnabled(bool enabled) = 0;
        virtual bool isBillboardRenderingEnabled() const = 0;

        virtual void setBillboardMaxDistance(float distance) = 0;

        virtual BillboardRenderStats getBillboardStats() const = 0;
    };
}
