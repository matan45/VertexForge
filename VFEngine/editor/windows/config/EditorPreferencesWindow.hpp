#pragma once
#include "SettingsEntry.hpp"
#include "config/EditorPreferences.hpp"
#include "data/EditorKeybindingTypes.hpp"
#include "data/ActionMappingTypes.hpp"
#include <string>
#include <vector>

namespace windows
{
    class EditorPreferencesWindow
    {
    public:
        enum Category
        {
            General = 0,
            Appearance,
            Input,
            Rendering,
            Editor,
            Debug,
            Keybindings,
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

        // Keybinding UI state
        bool waitingForKey = false;
        std::string captureAction;
        bool showConflictModal = false;
        services::InputBinding pendingBinding;
        std::string pendingAction;
        std::vector<services::KeybindingConflict> pendingConflicts;

        void loadSettings();
        void saveSettings();
        void resetToDefaults();
        void markDirty();

        void drawSearchBar();
        void drawCategoryList();
        void drawCategoryContent();
        void drawButtonBar();
        void drawSearchResults();

        void drawGeneralSection();
        void drawAppearanceSection();
        void drawInputSection();
        void drawRenderingSection();
        void drawEditorSection();
        void drawDebugSection();
        void drawKeybindingsSection();

        void buildSettingsRegistry();
        bool matchesSearch(const SettingsEntry& entry, const std::string& queryLower) const;

    public:
        void draw();
        void show();
    };
}
