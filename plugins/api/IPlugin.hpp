#pragma once
#include <string>
#include <cstdint>

namespace plugin {

    class PluginContext;

    struct PluginInfo
    {
        std::string name;
        std::string author;
        std::string description;
        uint32_t versionMajor = 1;
        uint32_t versionMinor = 0;
        uint32_t versionPatch = 0;
    };

    class IPlugin
    {
    public:
        virtual ~IPlugin() = default;

        // Return plugin metadata.
        virtual PluginInfo getInfo() const = 0;

        // Called after all engine services are initialized.
        // Register extensions (windows, event handlers, import stages, etc.) here.
        // Return false to indicate initialization failure (plugin will be unloaded).
        virtual bool onInitialize(PluginContext* context) = 0;

        // Called each frame. Optional - default does nothing.
        virtual void onUpdate(float deltaTime) { (void)deltaTime; }

        // Called when the plugin is about to be unloaded. Clean up resources here.
        virtual void onShutdown() = 0;
    };

}
