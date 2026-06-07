#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "api/PluginDescriptor.hpp"
#include <vector>
#include <string>

namespace windows
{
    class PluginManagerWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        PluginManagerWindow() = default;
        ~PluginManagerWindow() override = default;

        void draw() override;
        void drawContent();
        void show() { visible = true; needsRefresh = true; }
        // VK-1365: re-read per-scene overrides after a scene load/clear.
        void notifySceneLoaded() { needsRefresh = true; }

    private:
        // VK-1365: per-scene tri-state — Inherit follows the global .vfplugin flag,
        // On/Off are explicit overrides stored in the scene's .vfSettings.
        enum class SceneOverride { Inherit, On, Off };

        struct PluginEntry
        {
            plugin::PluginDescriptor descriptor;
            bool isLoaded = false;
            bool isInitialized = false;
            std::string status; // "Loaded", "Disabled", "API Mismatch", etc.
            SceneOverride sceneOverride = SceneOverride::Inherit;
        };

        bool visible = false;
        bool needsRefresh = true;
        std::vector<PluginEntry> entries;
        std::string currentScenePath;

        void refresh();
        void drawPluginEntry(PluginEntry& entry);
        void drawSceneOverrideCombo(PluginEntry& entry);
        void writeEnabledState(PluginEntry& entry, bool enabled);
        void saveToScene();
    };
}
