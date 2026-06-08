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

        // Per-scene soft-disable lifecycle (VK-1365). Optional - default no-ops.
        //
        // The DLL stays loaded across deactivate/activate cycles: registered windows,
        // event subscriptions, component bridges, custom pipelines/meshes, textures and
        // script natives all remain valid and must NOT be released in onDeactivate.
        //
        // While inactive the engine guarantees: onUpdate is not called, the plugin's
        // editor windows are hidden, its render-pass hooks are skipped, and its bound
        // world mask is suppressed (treated as 1.0). Plugins should quiesce state the
        // engine cannot see (pause/stop audio, VFX, physics bodies; mute event-handler
        // side effects) in onDeactivate and resume it in onActivate.
        //
        // Both hooks must be idempotent and must not assume a scene is loaded.
        // Order: onDeactivate runs before engine suppression; onActivate runs after
        // engine channels are restored (so re-binding/enqueuing hits live channels).
        virtual void onActivate() {}
        virtual void onDeactivate() {}

        // Called when the plugin is about to be unloaded. Clean up resources here.
        virtual void onShutdown() = 0;
    };

}
