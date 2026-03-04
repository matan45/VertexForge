#include "RenderHookAdapter.hpp"
#include "../../controllers/OffScreen.hpp"

namespace core
{
    RenderHookAdapter::RenderHookAdapter(controllers::OffScreen* offScreen)
        : offScreen(offScreen)
    {
    }

    plugin::RenderHookHandle RenderHookAdapter::registerHook(
        plugin::RenderPassHookPoint hookPoint,
        plugin::RenderHookCallback callback)
    {
        if (!offScreen) return {};
        return offScreen->registerRenderHook(hookPoint, std::move(callback));
    }

    void RenderHookAdapter::unregisterHook(plugin::RenderHookHandle handle)
    {
        if (offScreen)
        {
            offScreen->unregisterRenderHook(handle);
        }
    }
}
