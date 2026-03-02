#pragma once
#include "../data/RenderHookTypes.hpp"

namespace services {

    class IRenderHookProvider {
    public:
        virtual ~IRenderHookProvider() = default;

        virtual plugin::RenderHookHandle registerHook(
            plugin::RenderPassHookPoint hookPoint,
            plugin::RenderHookCallback callback) = 0;

        virtual void unregisterHook(plugin::RenderHookHandle handle) = 0;
    };

}
