#pragma once
#include "SettingsEntry.hpp"
#include "config/EditorPreferences.hpp"
#include <imgui.h>
#include <string>
#include <vector>

namespace windows
{
    class EditorPreferencesWindow
    {
    public:
        enum Category
        {
            Appearance = 0,
            Debug,
            WindowLayout,
            COUNT
        };

    private:
        bool visible = false;
        config::EditorPreferences settings;
        config::EditorPreferences savedSettings;
        bool settingsLoaded = false;
        bool isDirty = false;
        int selectedCategory = 0;
        std::string searchQuery;
        std::vector<SettingsEntry> settingsRegistry;
        bool registryBuilt = false;

        // Dockspace id used by the Window Layout section's "Reset to default layout"
        ImGuiID dockSpaceId = 0;

        // Absolute path of the preferences JSON, cached for the footer display.
        std::string settingsPath;

        // Window Layout UI state
        char saveLayoutNameBuffer[128] = {};

        void loadSettings();
        void saveSettings();
        void resetToDefaults();
        void markDirty();

        void drawSearchBar();
        void drawCategoryList();
        void drawCategoryContent();
        void drawButtonBar();
        void drawSearchResults();

        void drawAppearanceSection();
        void drawDebugSection();
        void drawWindowLayoutSection();

        void buildSettingsRegistry();
        bool matchesSearch(const SettingsEntry& entry, const std::string& queryLower) const;

    public:
        void draw();
        void show();
        void setDockSpaceId(ImGuiID id) { dockSpaceId = id; }
    };
}
