#include "PluginComponentRegistry.hpp"
#include "print/Log.hpp"

namespace plugin
{
    PluginComponentRegistry& PluginComponentRegistry::instance()
    {
        static PluginComponentRegistry registry;
        return registry;
    }

    void PluginComponentRegistry::registerComponent(const PluginComponentInfo& info)
    {
        std::unique_lock lock(mutex);

        if (components.contains(info.qualifiedName))
        {
            vfLogWarning("Plugin component '{}' already registered, replacing", info.qualifiedName);
        }

        components[info.qualifiedName] = info;
        vfLogInfo("Registered plugin component: {}", info.qualifiedName);
    }

    void PluginComponentRegistry::unregisterPlugin(const std::string& pluginName)
    {
        std::unique_lock lock(mutex);

        std::erase_if(components, [&](const auto& pair)
        {
            return pair.second.pluginName == pluginName;
        });

        vfLogInfo("Unregistered all components for plugin: {}", pluginName);
    }

    const PluginComponentInfo* PluginComponentRegistry::findComponent(const std::string& qualifiedName) const
    {
        std::shared_lock lock(mutex);

        auto it = components.find(qualifiedName);
        if (it != components.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    std::vector<std::string> PluginComponentRegistry::getAllComponentNames() const
    {
        std::shared_lock lock(mutex);

        std::vector<std::string> names;
        names.reserve(components.size());
        for (const auto& [name, _] : components)
        {
            names.push_back(name);
        }
        return names;
    }
}
