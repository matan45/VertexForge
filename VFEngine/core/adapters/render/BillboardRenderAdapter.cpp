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

    services::ImposterBakeResult BillboardRenderAdapter::bakeImposter(const std::string& meshPath,
                                                                        const std::string& outputPath,
                                                                        const glm::vec3& meshCenter,
                                                                        float meshScale)
    {
        if (!offScreen)
        {
            services::ImposterBakeResult result;
            result.success = false;
            result.errorMessage = "No OffScreenController available";
            return result;
        }

        auto controllerResult = offScreen->bakeImposter(meshPath, outputPath, meshCenter, meshScale);

        services::ImposterBakeResult result;
        result.success = controllerResult.success;
        result.outputPath = controllerResult.outputPath;
        result.errorMessage = controllerResult.errorMessage;
        return result;
    }
}
