#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

// VK-1651: generic access to plugin-authored (EnTT-meta reflected) components.
// Payloads are JSON only so callers never see entt::meta or plugin types; the
// handler lives in Editor.exe (editor/handlers/PluginComponentHandler) because the
// component bridges are owned by the Plugin StaticLib linked there.
//
// Field JSON uses the same format as scene serialization of plugin components
// (serialization::meta::serializeMetaAny; AssetRef fields carry a "<name>Path" sibling).
namespace events::scene {

    struct PluginComponentResult {
        bool ok = false;
        std::string error;            // set when !ok
        nlohmann::json value;         // the component's fields after the operation (object)
    };

    // [{ "name", "plugin", "fields": [{ "name", "type", "label"?, "readOnly"?, "hidden"?, "min"?, "max"? }] }]
    struct GetPluginComponentTypesQuery : IQuery<nlohmann::json> {
        std::string_view getName() const override { return "GetPluginComponentTypes"; }
    };

    // nullopt when the entity is invalid, the type is unknown, or the entity lacks the component.
    struct GetPluginComponentQuery : IQuery<std::optional<nlohmann::json>> {
        services::EntityHandle entity;
        std::string type;

        std::string_view getName() const override { return "GetPluginComponent"; }
    };

    // Strict partial update: every field is validated before any is written. Unknown
    // fields, wrong JSON types and read-only fields fail the whole command.
    struct SetPluginComponentFieldsCommand : ICommand<PluginComponentResult> {
        services::EntityHandle entity;
        std::string type;
        nlohmann::json fields;  // object

        std::string_view getName() const override { return "SetPluginComponentFields"; }
    };

    // Emplaces a default-constructed component, then applies `fields` (may be null)
    // with the same strict rules. Fails if the entity already has the component; on
    // a field error the component is removed again.
    struct AddPluginComponentCommand : ICommand<PluginComponentResult> {
        services::EntityHandle entity;
        std::string type;
        nlohmann::json fields;

        std::string_view getName() const override { return "AddPluginComponent"; }
    };

    // value = the fields the component had just before removal (for undo).
    struct RemovePluginComponentCommand : ICommand<PluginComponentResult> {
        services::EntityHandle entity;
        std::string type;

        std::string_view getName() const override { return "RemovePluginComponent"; }
    };
}
