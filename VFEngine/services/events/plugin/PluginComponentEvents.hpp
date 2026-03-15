#pragma once
#include "../EventTypes.hpp"
#include "data/EntityHandle.hpp"
#include "components/PluginPropertyTypes.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>

namespace events::plugin
{
    struct GetRegisteredPluginComponentsQuery : IQuery<std::vector<std::string>>
    {
        std::string_view getName() const override { return "GetRegisteredPluginComponents"; }
    };

    struct GetPluginComponentDataQuery : IQuery<std::optional<nlohmann::json>>
    {
        services::EntityHandle entity;
        std::string qualifiedName;

        std::string_view getName() const override { return "GetPluginComponentData"; }
    };

    struct AddPluginComponentCommand : ICommand<bool>
    {
        services::EntityHandle entity;
        std::string qualifiedName;

        std::string_view getName() const override { return "AddPluginComponent"; }
    };

    struct RemovePluginComponentCommand : ICommand<bool>
    {
        services::EntityHandle entity;
        std::string qualifiedName;

        std::string_view getName() const override { return "RemovePluginComponent"; }
    };

    struct UpdatePluginComponentDataCommand : ICommand<>
    {
        services::EntityHandle entity;
        std::string qualifiedName;
        nlohmann::json data;

        std::string_view getName() const override { return "UpdatePluginComponentData"; }
    };

    struct GetPluginComponentDescriptorsQuery : IQuery<std::vector<components::plugin::PropertyDescriptor>>
    {
        std::string qualifiedName;

        std::string_view getName() const override { return "GetPluginComponentDescriptors"; }
    };

    struct HasPluginInspectorQuery : IQuery<bool>
    {
        std::string qualifiedName;

        std::string_view getName() const override { return "HasPluginInspector"; }
    };

    struct InvokePluginInspectorCommand : ICommand<bool>
    {
        services::EntityHandle entity;
        std::string qualifiedName;

        std::string_view getName() const override { return "InvokePluginInspector"; }
    };
}
