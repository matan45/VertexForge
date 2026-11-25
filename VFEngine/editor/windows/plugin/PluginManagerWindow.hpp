#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "core/PluginRoots.hpp"
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
            // VK-1732: the descriptor, its root, and the state the loader gives it
            // (the overrides of shipped plugins applied, shadowing decided).
            plugin::DiscoveredPlugin plugin;
            bool isLoaded = false;
            bool isInitialized = false;
            std::string status; // "Loaded", "Disabled", "API Mismatch", "Shadowed", etc.
            SceneOverride sceneOverride = SceneOverride::Inherit;
            std::string error;  // the last failed toggle or reset, shown in red
        };

        bool visible = false;
        bool needsRefresh = true;
        std::vector<PluginEntry> entries;
        std::string currentScenePath;

        // VK-1284: "New Plugin..." scaffolding modal state
        static constexpr int capabilityCount = 10;
        bool openNewPluginPopup = false;
        bool newPluginSucceeded = false;
        char newPluginName[128] = {};
        char newPluginAuthor[128] = "VertexForge";
        bool newPluginCaps[capabilityCount] = {};
        bool newPluginComponent = true;
        bool newPluginEditorWindow = false;
        std::string newPluginResult; // error message, or created path after success
        // VK-1732: the plugin was made as a Standalone project (an installed engine),
        // built in this configuration.
        bool newPluginStandalone = false;
        std::string newPluginConfiguration;

        void refresh();
        void drawPluginEntry(PluginEntry& entry);
        void drawSceneOverrideCombo(PluginEntry& entry);
        void saveToScene();
        void drawNewPluginPopup();
        void resetNewPluginState();
    };
}
