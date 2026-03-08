#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "vegetation/GrassConfig.hpp"

namespace events::vegetation
{
    struct SetGrassConfigCommand : ICommand<void>
    {
        services::EntityHandle entityId;
        ::vegetation::GrassRenderConfig config;

        std::string_view getName() const override { return "SetGrassConfig"; }
    };

    struct GetGrassConfigQuery : IQuery<::vegetation::GrassRenderConfig>
    {
        services::EntityHandle entityId;

        std::string_view getName() const override { return "GetGrassConfig"; }
    };

    struct GrassConfigChangedNotification : INotification
    {
        services::EntityHandle entityId;

        std::string_view getName() const override { return "GrassConfigChanged"; }
    };
}
