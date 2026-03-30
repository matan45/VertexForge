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

    private:
        struct PluginEntry
        {
            plugin::PluginDescriptor descriptor;
            bool isLoaded = false;
            bool isInitialized = false;
            std::string status; // "Loaded", "Disabled", "API Mismatch", etc.
        };

        bool visible = false;
        bool needsRefresh = true;
        std::vector<PluginEntry> entries;

        void refresh();
        void drawPluginEntry(PluginEntry& entry);
        void writeEnabledState(PluginEntry& entry, bool enabled);
    };
}
