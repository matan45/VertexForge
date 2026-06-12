#include "EditorPreferencesWindow.hpp"
#include "SettingsTooltip.hpp"
#include "ThemeManager.hpp"
#include "config/EditorThemeStorage.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorSettingsEvents.hpp"
#include "events/save/ConfigEvents.hpp"
#include "string/StringUtil.hpp"
#include <imgui.h>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <windows.h>
#include <shellapi.h>

namespace windows
{
    static const char* categoryNames[] = {
        "Appearance",
        "Debug",
        "Window Layout"
    };

    void EditorPreferencesWindow::show()
    {
        visible = true;
        if (!settingsLoaded)
            loadSettings();
    }

    void EditorPreferencesWindow::loadSettings()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        settings = dispatcher.query(events::editor::GetEditorSettingsQuery{});
        savedSettings = settings;
        settingsLoaded = true;
        isDirty = false;

        refreshThemeList();
        editingCustomTheme = false;
        if (settings.appearance.theme != "Dark" && settings.appearance.theme != "Light")
        {
            auto loaded = config::EditorThemeStorage::load(
                config::EditorThemeStorage::defaultThemesDirectory(), settings.appearance.theme);
            if (loaded)
            {
                editedTheme = *loaded;
                editingCustomTheme = true;
            }
        }

        settingsPath = dispatcher.query(events::editor::GetEditorSettingsPathQuery{});

        events::save::GetConfigIntQuery tabQuery;
        tabQuery.key = "editorPreferences_lastCategory";
        tabQuery.defaultValue = 0;
        selectedCategory = static_cast<int>(dispatcher.query(tabQuery));
        if (selectedCategory < 0 || selectedCategory >= Category::COUNT)
            selectedCategory = 0;

        if (!registryBuilt)
        {
            buildSettingsRegistry();
            registryBuilt = true;
        }
    }

    void EditorPreferencesWindow::saveSettings()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Persist the edited theme before dispatching: the settings-changed
        // notification re-applies the theme from disk and must see fresh colors.
        if (editingCustomTheme)
            config::EditorThemeStorage::save(config::EditorThemeStorage::defaultThemesDirectory(), editedTheme);

        events::editor::SetEditorSettingsCommand cmd;
        cmd.settings = settings;
        dispatcher.execute(cmd);

        savedSettings = settings;
        isDirty = false;
        themePreviewActive = false;

        events::save::SetConfigIntCommand tabCmd;
        tabCmd.key = "editorPreferences_lastCategory";
        tabCmd.value = selectedCategory;
        dispatcher.execute(tabCmd);
    }

    void EditorPreferencesWindow::resetToDefaults()
    {
        settings = config::EditorPreferences::createDefault();
        isDirty = true;
        editingCustomTheme = false;
        ThemeManager::applyTheme(settings.appearance);
        themePreviewActive = true;
    }

    void EditorPreferencesWindow::refreshThemeList()
    {
        customThemes = config::EditorThemeStorage::listThemes(config::EditorThemeStorage::defaultThemesDirectory());
    }

    void EditorPreferencesWindow::selectTheme(const std::string& name)
    {
        settings.appearance.theme = name;
        editingCustomTheme = false;
        if (name != "Dark" && name != "Light")
        {
            auto loaded = config::EditorThemeStorage::load(config::EditorThemeStorage::defaultThemesDirectory(), name);
            if (loaded)
            {
                editedTheme = *loaded;
                editingCustomTheme = true;
            }
        }
        ThemeManager::applyTheme(settings.appearance);
        themePreviewActive = true;
        themeError.clear();
        markDirty();
    }

    void EditorPreferencesWindow::createThemeFromCurrent()
    {
        std::string name = config::EditorThemeStorage::sanitizeName(newThemeNameBuffer);
        if (name.empty())
        {
            themeError = "Invalid or reserved theme name";
            return;
        }
        if (std::find(customThemes.begin(), customThemes.end(), name) != customThemes.end())
        {
            themeError = "A theme with this name already exists";
            return;
        }

        std::string basedOn = "Dark";
        if (settings.appearance.theme == "Light" || (editingCustomTheme && editedTheme.basedOn == "Light"))
            basedOn = "Light";

        config::EditorTheme theme = ThemeManager::snapshotCurrentStyle(name, basedOn);
        if (!config::EditorThemeStorage::save(config::EditorThemeStorage::defaultThemesDirectory(), theme))
        {
            themeError = "Failed to save theme file";
            return;
        }

        themeError.clear();
        newThemeNameBuffer[0] = '\0';
        refreshThemeList();
        editedTheme = theme;
        editingCustomTheme = true;
        settings.appearance.theme = name;
        themePreviewActive = true;
        markDirty();
    }

    void EditorPreferencesWindow::deleteSelectedTheme()
    {
        config::EditorThemeStorage::remove(config::EditorThemeStorage::defaultThemesDirectory(), settings.appearance.theme);
        refreshThemeList();
        selectTheme("Dark");
    }

    void EditorPreferencesWindow::revertThemePreview()
    {
        if (!themePreviewActive)
            return;

        ThemeManager::applyTheme(savedSettings.appearance);
        editingCustomTheme = false;
        if (savedSettings.appearance.theme != "Dark" && savedSettings.appearance.theme != "Light")
        {
            auto loaded = config::EditorThemeStorage::load(
                config::EditorThemeStorage::defaultThemesDirectory(), savedSettings.appearance.theme);
            if (loaded)
            {
                editedTheme = *loaded;
                editingCustomTheme = true;
            }
        }
        themePreviewActive = false;
    }

    void EditorPreferencesWindow::markDirty()
    {
        isDirty = true;
    }

    void EditorPreferencesWindow::drawSearchBar()
    {
        ImGui::Text("Search:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        char buffer[256];
        std::strncpy(buffer, searchQuery.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
        if (ImGui::InputText("##PreferencesSearch", buffer, sizeof(buffer)))
        {
            searchQuery = std::string(buffer);
        }
        ImGui::Spacing();
    }

    void EditorPreferencesWindow::drawCategoryList()
    {
        for (int i = 0; i < Category::COUNT; ++i)
        {
            bool isSelected = (selectedCategory == i);
            if (ImGui::Selectable(categoryNames[i], isSelected))
            {
                selectedCategory = i;
            }
        }
    }

    void EditorPreferencesWindow::drawCategoryContent()
    {
        ImGui::Text("%s", categoryNames[selectedCategory]);
        ImGui::Separator();
        ImGui::Spacing();

        switch (selectedCategory)
        {
        case Appearance:    drawAppearanceSection(); break;
        case Debug:         drawDebugSection(); break;
        case WindowLayout:  drawWindowLayoutSection(); break;
        default: break;
        }
    }

    void EditorPreferencesWindow::drawButtonBar()
    {
        ImGui::Separator();
        ImGui::Spacing();

        if (!settingsPath.empty())
        {
            ImGui::TextDisabled("Saved to: %s", settingsPath.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Open Folder"))
            {
                std::string folder = std::filesystem::path(settingsPath).parent_path().string();
                ShellExecuteA(nullptr, "explore", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            ImGui::Spacing();
        }

        if (ImGui::Button("Reset Defaults", ImVec2(110, 0)))
            resetToDefaults();

        if (isDirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)");
        }

        ImGui::SameLine(ImGui::GetContentRegionMax().x - 170.0f);

        if (ImGui::Button("Cancel", ImVec2(80, 0)))
        {
            settings = savedSettings;
            isDirty = false;
            visible = false;
        }

        ImGui::SameLine();

        if (ImGui::Button("Apply", ImVec2(80, 0)))
            saveSettings();
    }

    void EditorPreferencesWindow::drawSearchResults()
    {
        std::string queryLower = StringUtil::toLower(searchQuery);
        bool anyMatch = false;

        for (auto& entry : settingsRegistry)
        {
            if (!matchesSearch(entry, queryLower))
                continue;

            anyMatch = true;
            ImGui::PushID(entry.name.c_str());
            ImGui::TextDisabled("[%s]", categoryNames[entry.category]);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.3f, 1.0f), "%s", entry.name.c_str());
            entry.drawFunction();
            ImGui::Separator();
            ImGui::PopID();
        }

        if (!anyMatch)
        {
            ImGui::TextDisabled("No settings match \"%s\"", searchQuery.c_str());
        }
    }

    bool EditorPreferencesWindow::matchesSearch(const SettingsEntry& entry, const std::string& queryLower) const
    {
        if (StringUtil::toLower(entry.name).find(queryLower) != std::string::npos)
            return true;

        if (StringUtil::toLower(entry.description).find(queryLower) != std::string::npos)
            return true;

        for (const auto& tag : entry.tags)
        {
            if (StringUtil::toLower(tag).find(queryLower) != std::string::npos)
                return true;
        }

        return false;
    }

    void EditorPreferencesWindow::draw()
    {
        if (!visible)
            return;

        ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Preferences", &visible))
        {
            drawSearchBar();

            const float footerReserveHeight =
                ImGui::GetFrameHeightWithSpacing() * (settingsPath.empty() ? 2.0f : 3.0f);

            if (!searchQuery.empty())
            {
                ImGui::BeginChild("SearchResults", ImVec2(0, -footerReserveHeight), true);
                drawSearchResults();
                ImGui::EndChild();
            }
            else
            {
                float leftPanelWidth = 160.0f;
                float contentHeight = -footerReserveHeight;

                ImGui::BeginChild("CategoryList", ImVec2(leftPanelWidth, contentHeight), true);
                drawCategoryList();
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("SettingsContent", ImVec2(0, contentHeight), true);
                drawCategoryContent();
                ImGui::EndChild();
            }

            drawButtonBar();
        }
        ImGui::End();

        // Cancel button or close-via-X while a live theme preview is active:
        // restore the last applied appearance.
        if (!visible && themePreviewActive)
        {
            settings = savedSettings;
            isDirty = false;
            revertThemePreview();
        }
    }
}
