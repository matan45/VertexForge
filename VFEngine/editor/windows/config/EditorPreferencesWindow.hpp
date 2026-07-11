#pragma once
#include "SettingsEntry.hpp"
#include "config/EditorPreferences.hpp"
#include "config/EditorTheme.hpp"
#include "data/EditorKeybindingTypes.hpp"
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
            EditorShortcuts,
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

        // Custom theme UI state
        std::vector<std::string> customThemes;
        config::EditorTheme editedTheme;      // in-memory copy of the selected custom theme
        bool editingCustomTheme = false;
        char newThemeNameBuffer[128] = {};
        std::string themeError;
        std::string themeColorFilter;
        bool themePreviewActive = false;      // live preview diverges from savedSettings

        // Editor shortcut UI state. Shortcut edits are applied and persisted
        // immediately through EditorKeybindingServiceImpl.
        std::vector<services::EditorActionInfo> shortcutActions;
        std::string shortcutCaptureAction;
        std::string shortcutMessage;
        bool shortcutRefreshPending = false;

        void loadSettings();
        void saveSettings();
        void resetToDefaults();
        void markDirty();

        void refreshThemeList();
        void selectTheme(const std::string& name);
        void createThemeFromCurrent();
        void deleteSelectedTheme();
        void revertThemePreview();
        void drawThemeCombo(const char* label);
        void drawThemeManagement();
        void drawThemeColorEditor();

        void drawSearchBar();
        void drawCategoryList();
        void drawCategoryContent();
        void drawButtonBar();
        void drawSearchResults();

        void drawAppearanceSection();
        void drawEditorShortcutsSection();
        void drawDebugSection();
        void drawWindowLayoutSection();

        void refreshShortcutActions();
        void captureShortcutBinding();
        void applyShortcutBinding(const services::InputBinding& binding);
        void resetShortcut(const std::string& actionName);
        void resetAllShortcuts();
        std::string shortcutBindingLabel(const services::InputBinding& binding) const;
        std::string shortcutActionLabel(const std::string& actionName) const;

        void buildSettingsRegistry();
        bool matchesSearch(const SettingsEntry& entry, const std::string& queryLower) const;

    public:
        void draw();
        void show();
        void setDockSpaceId(ImGuiID id) { dockSpaceId = id; }
    };
}
