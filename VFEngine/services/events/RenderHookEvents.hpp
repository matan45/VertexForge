#pragma once
#include "EventTypes.hpp"
#include "../data/RenderHookTypes.hpp"

namespace events::renderhook {

    struct RegisterRenderPassHookCommand : ICommand<plugin::RenderHookHandle> {
        plugin::RenderPassHookPoint hookPoint;
        mutable plugin::RenderHookCallback callback;

        std::string_view getName() const override { return "RegisterRenderPassHook"; }
    };

    struct UnregisterRenderPassHookCommand : ICommand<> {
        plugin::RenderHookHandle handle;

        std::string_view getName() const override { return "UnregisterRenderPassHook"; }
    };

}
