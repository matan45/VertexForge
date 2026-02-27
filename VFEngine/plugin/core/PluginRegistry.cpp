#include "PluginRegistry.hpp"

namespace plugin {

    void PluginRegistry::addPlugin(LoadedPlugin plugin)
    {
        plugins.push_back(std::move(plugin));
    }

    std::vector<LoadedPlugin>& PluginRegistry::getPlugins()
    {
        return plugins;
    }

    const std::vector<LoadedPlugin>& PluginRegistry::getPlugins() const
    {
        return plugins;
    }

    size_t PluginRegistry::getPluginCount() const
    {
        return plugins.size();
    }

    void PluginRegistry::clear()
    {
        plugins.clear();
    }

}
