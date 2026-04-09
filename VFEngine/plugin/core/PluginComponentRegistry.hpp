#pragma once
#include <nlohmann/json.hpp>
#include <entt/entt.hpp>
#include "components/PluginPropertyTypes.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <shared_mutex>

namespace plugin
{
    struct PluginComponentInfo
    {
        std::string pluginName;
        std::string componentName;
        std::string qualifiedName; // "pluginName::componentName"
        std::vector<components::plugin::PropertyDescriptor> properties;
        nlohmann::json defaultData; // built from property defaults
        std::function<bool(nlohmann::json&)> inspector; // optional custom override

        // Lifecycle hooks (optional, all execute in exe address space)
        std::function<void(entt::entity, const nlohmann::json&)> onAdded;
        std::function<void(entt::entity)> onRemoved;
        std::function<void(entt::entity, const nlohmann::json&)> onDataChanged;
    };

    class PluginComponentRegistry
    {
    public:
        static PluginComponentRegistry& instance();

        void registerComponent(const PluginComponentInfo& info);
        void unregisterPlugin(const std::string& pluginName);

        const PluginComponentInfo* findComponent(const std::string& qualifiedName) const;
        std::vector<std::string> getAllComponentNames() const;

    private:
        PluginComponentRegistry() = default;

        mutable std::shared_mutex mutex;
        std::unordered_map<std::string, PluginComponentInfo> components;
    };
}
