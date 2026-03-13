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

    // Global versions — operate on first entity with GrassComponent (no entity handle needed)
    struct SetGlobalGrassConfigCommand : ICommand<void>
    {
        ::vegetation::GrassRenderConfig config;

        std::string_view getName() const override { return "SetGlobalGrassConfig"; }
    };

    struct GetGlobalGrassConfigQuery : IQuery<::vegetation::GrassRenderConfig>
    {
        std::string_view getName() const override { return "GetGlobalGrassConfig"; }
    };
}
