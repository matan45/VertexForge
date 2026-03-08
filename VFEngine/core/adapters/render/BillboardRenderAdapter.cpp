#include "BillboardRenderAdapter.hpp"
#include "../../../graphics/controllers/OffScreenController.hpp"
#include "print/Log.hpp"

namespace core::adapters
{
    void BillboardRenderAdapter::setBillboardRenderingEnabled(bool enabled)
    {
        billboardEnabled = enabled;
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
            offScreen->setCategoryDistance(5, distance); // Billboard category = 5
        }
    }

    services::BillboardRenderStats BillboardRenderAdapter::getBillboardStats() const
    {
        services::BillboardRenderStats stats;
        stats.renderingEnabled = billboardEnabled;
        return stats;
    }

    services::ImposterBakeResult BillboardRenderAdapter::bakeImposter(const std::string& meshPath,
                                                                        const std::string& outputPath)
    {
        if (!offScreen)
        {
            services::ImposterBakeResult result;
            result.success = false;
            result.errorMessage = "No OffScreenController available";
            return result;
        }

        auto controllerResult = offScreen->bakeImposter(meshPath, outputPath);

        services::ImposterBakeResult result;
        result.success = controllerResult.success;
        result.outputPath = controllerResult.outputPath;
        result.errorMessage = controllerResult.errorMessage;
        return result;
    }
}
