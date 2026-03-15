#pragma once
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <string>

namespace components
{
    struct PluginComponentsComponent
    {
        // key = "pluginName::componentName", value = component data as JSON
        std::unordered_map<std::string, nlohmann::json> components;
    };
}
