#pragma once
#include "../EventTypes.hpp"
#include <cstdint>
#include <string>
#include <vector>

// VK-1655: list and toggle plugin post-process effects (VK-1409) from outside the plugin, e.g. the
// MCP tools. The effects and the plugin's intent live in plugin/core/PluginContextImpl; Editor.exe
// reaches them only through these events (handled by editor/handlers/PluginPostProcessEffectHandler).
// The enabled state is session-only, like the plugin's own toggles: it is never saved with the scene.
namespace events::editor {

    struct PluginPostProcessEffectInfo {
        uint64_t id = 0;               // plugin::PostProcessEffectHandle::id, valid for this session
        std::string plugin;            // the owning plugin's name
        std::string name;              // PostProcessEffectDesc::debugName (may be empty or repeat)
        uint32_t priority = 0;         // chain order; the built-in tonemap runs at 100
        bool requestedEnabled = false; // what the plugin (or the last toggle) asked for
        bool pluginActive = false;     // false while the plugin is deactivated for this scene (VK-1365)
    };

    // Every effect of every loaded plugin, in plugin load order.
    struct ListPluginPostProcessEffectsQuery : IQuery<std::vector<PluginPostProcessEffectInfo>> {
        std::string_view getName() const override { return "ListPluginPostProcessEffects"; }
    };

    // Main thread. Records the request like the plugin's own setPostProcessEffectEnabled (it renders
    // only while the plugin is active) and tells the plugin through the PluginEventBus event
    // "vf.postprocess.effectEnabled" {id, plugin, name, enabled}. False = no effect has this id.
    struct SetPluginPostProcessEffectEnabledCommand : ICommand<bool> {
        uint64_t id = 0;
        bool enabled = true;

        std::string_view getName() const override { return "SetPluginPostProcessEffectEnabled"; }
    };
}
