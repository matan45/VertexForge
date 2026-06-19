#pragma once
#include "../EventTypes.hpp"
#include "../../data/PostProcessEffectTypes.hpp"
#include <cstddef>
#include <vector>

namespace events::postprocessfx {

    struct RegisterPostProcessEffectCommand : ICommand<plugin::PostProcessEffectHandle> {
        mutable plugin::PostProcessEffectDesc desc;

        std::string_view getName() const override { return "RegisterPostProcessEffect"; }
    };

    struct UpdatePostProcessEffectParamsCommand : ICommand<> {
        plugin::PostProcessEffectHandle handle;
        mutable std::vector<std::byte> params;

        std::string_view getName() const override { return "UpdatePostProcessEffectParams"; }
    };

    struct SetPostProcessEffectEnabledCommand : ICommand<> {
        plugin::PostProcessEffectHandle handle;
        bool enabled = true;

        std::string_view getName() const override { return "SetPostProcessEffectEnabled"; }
    };

    struct UnregisterPostProcessEffectCommand : ICommand<> {
        plugin::PostProcessEffectHandle handle;

        std::string_view getName() const override { return "UnregisterPostProcessEffect"; }
    };

}
