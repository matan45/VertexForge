#include "BillboardRenderAdapter.hpp"
#include "../../controllers/OffScreen.hpp"
#include "../../../services/data/CullingCategories.hpp"
#include "print/Log.hpp"

namespace core::adapters
{

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
            offScreen->setCategoryDistance(services::CullingCategory::Billboard, distance);
        }
    }

    services::BillboardRenderStats BillboardRenderAdapter::getBillboardStats() const
    {
        services::BillboardRenderStats stats;
        stats.renderingEnabled = billboardEnabled;
        return stats;
    }

}
