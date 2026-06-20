#include "PostProcessEffectAdapter.hpp"
#include "../../controllers/OffScreen.hpp"
#include "../../../graphics/render/RenderPassHandler.hpp"

namespace core
{
    PostProcessEffectAdapter::PostProcessEffectAdapter(controllers::OffScreen* offScreen)
        : offScreen(offScreen)
    {
    }

    plugin::PostProcessEffectHandle PostProcessEffectAdapter::registerEffect(const plugin::PostProcessEffectDesc& desc)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return {};
        return handler->registerPostProcessEffect(desc);
    }

    void PostProcessEffectAdapter::updateEffectParams(plugin::PostProcessEffectHandle handle,
                                                      std::vector<std::byte>&& params)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return;
        handler->updatePostProcessEffectParams(handle, std::move(params));
    }

    void PostProcessEffectAdapter::setEffectEnabled(plugin::PostProcessEffectHandle handle, bool enabled)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return;
        handler->setPostProcessEffectEnabled(handle, enabled);
    }

    void PostProcessEffectAdapter::unregisterEffect(plugin::PostProcessEffectHandle handle)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return;
        handler->unregisterPostProcessEffect(handle);
    }
}
