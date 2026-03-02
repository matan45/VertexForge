#pragma once
#include "../../services/providers/IRenderHookProvider.hpp"

namespace controllers
{
    class OffScreen;
}

namespace core
{
    class RenderHookAdapter : public services::IRenderHookProvider
    {
    private:
        controllers::OffScreen* offScreen;

    public:
        explicit RenderHookAdapter(controllers::OffScreen* offScreen);
        ~RenderHookAdapter() override = default;

        plugin::RenderHookHandle registerHook(
            plugin::RenderPassHookPoint hookPoint,
            plugin::RenderHookCallback callback) override;

        void unregisterHook(plugin::RenderHookHandle handle) override;
    };
}
