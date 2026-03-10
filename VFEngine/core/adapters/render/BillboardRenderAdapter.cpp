#include "BillboardRenderAdapter.hpp"
#include "../../controllers/OffScreen.hpp"
#include "print/Log.hpp"

namespace core::adapters
{
    // Culling category indices (must match OffScreenControllerSettings.cpp)
    static constexpr uint32_t kBillboardCullingCategory = 5;

    void BillboardRenderAdapter::setBillboardRenderingEnabled(bool enabled)
    {
        billboardEnabled = enabled;
        if (offScreen)
        {
            offScreen->setBillboardRenderingEnabled(enabled);
        }
    }

    bool BillboardRenderAdapter::isBillboardRenderingEnabled() const
    {
        return billboardEnabled;
    }

    void BillboardRenderAdapter::setBillboardMaxDistance(float distance)
    {
        maxDistance = distance;
        if (offScreen)
        {
            offScreen->setCategoryDistance(kBillboardCullingCategory, distance);
        }
    }

    services::BillboardRenderStats BillboardRenderAdapter::getBillboardStats() const
    {
        services::BillboardRenderStats stats;
        stats.renderingEnabled = billboardEnabled;
        return stats;
    }

}
