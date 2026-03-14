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
}
