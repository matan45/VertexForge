#pragma once
#include "PluginRegistry.hpp"
#include <filesystem>
#include <unordered_set>
#include <string>

namespace plugin {

    class PluginManager
    {
    public:
        PluginManager();
        ~PluginManager();

        // Set available engine capabilities (e.g., "editor", "audio", "physics", "import")
        void setCapabilities(const std::unordered_set<std::string>& caps);

        // Discover and load all plugin DLLs from the given directory.
        void loadAll(const std::filesystem::path& pluginDirectory);

        // Load a single plugin DLL.
        bool loadPlugin(const std::filesystem::path& dllPath);

        // Initialize all loaded plugins (call onInitialize with context).
        void initializeAll();

        // Called each frame.
        void updateAll(float deltaTime);

        // Shutdown and unload all plugins in reverse order.
        void shutdownAll();

        // Query loaded plugins.
        const std::vector<LoadedPlugin>& getLoadedPlugins() const;
        size_t getPluginCount() const;

    private:
        PluginRegistry registry;
        std::unordered_set<std::string> capabilities;

        bool validatePluginVersion(DynamicLibrary& lib) const;
    };

}
