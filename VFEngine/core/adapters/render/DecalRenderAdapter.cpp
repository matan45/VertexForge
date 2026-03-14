#include "DecalRenderAdapter.hpp"
#include "../../controllers/OffScreen.hpp"

namespace core::adapters
{
    void DecalRenderAdapter::setDecalRenderingEnabled(bool enabled)
    {
        decalEnabled = enabled;
        if (offScreen)
        {
            offScreen->setDecalRenderingEnabled(enabled);
        }
    }

    bool DecalRenderAdapter::isDecalRenderingEnabled() const
    {
        return decalEnabled;
    }

    void DecalRenderAdapter::updateDecals(std::vector<services::DecalRenderData>&& decals)
    {
        currentDecals = std::move(decals);
        if (offScreen)
        {
            offScreen->setDecalDrawList(currentDecals);
        }
    }
}
