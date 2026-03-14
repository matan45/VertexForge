#pragma once

#include "../EventTypes.hpp"
#include "../../providers/render/IDecalRenderProvider.hpp"

namespace events::render
{
    struct SetDecalRenderingEnabledCommand : ICommand<>
    {
        bool enabled = true;
        std::string_view getName() const override { return "SetDecalRenderingEnabledCommand"; }
    };

    struct IsDecalRenderingEnabledQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsDecalRenderingEnabledQuery"; }
    };
}
