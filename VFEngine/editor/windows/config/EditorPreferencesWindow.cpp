#include "EditorPreferencesWindow.hpp"
#include "SettingsTooltip.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorSettingsEvents.hpp"
#include "events/save/ConfigEvents.hpp"
#include "string/StringUtil.hpp"
#include <imgui.h>
#include <cstring>
#include <algorithm>

namespace windows
{
    static const char* categoryNames[] = {
        "General",
        "Appearance",
        "Input",
        "Rendering",
        "Editor",
        "Debug",
        "Keybindings"
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

        events::editor::SetEditorSettingsCommand cmd;
        cmd.settings = settings;
        dispatcher.execute(cmd);

        savedSettings = settings;
        isDirty = false;

        events::save::SetConfigIntCommand tabCmd;
        tabCmd.key = "editorPreferences_lastCategory";
        tabCmd.value = selectedCategory;
        dispatcher.execute(tabCmd);
    }

    void EditorPreferencesWindow::resetToDefaults()
    {
        settings = config::EditorPreferences::createDefault();
        isDirty = true;
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
        case General:       drawGeneralSection(); break;
        case Appearance:    drawAppearanceSection(); break;
        case Input:         drawInputSection(); break;
        case Rendering:     drawRenderingSection(); break;
        case Editor:        drawEditorSection(); break;
        case Debug:         drawDebugSection(); break;
        case Keybindings:   drawKeybindingsSection(); break;
        default: break;
        }
    }

    void EditorPreferencesWindow::drawButtonBar()
    {
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Reset Defaults", ImVec2(110, 0)))
            resetToDefaults();

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 170.0f);

        if (ImGui::Button("Cancel", ImVec2(80, 0)))
        {
            settings = savedSettings;
            isDirty = false;
            visible = false;
        }

        ImGui::SameLine();

        if (ImGui::Button("Apply", ImVec2(80, 0)))
            saveSettings();

        if (isDirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)");
        }
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

            if (!searchQuery.empty())
            {
                ImGui::BeginChild("SearchResults", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 8.0f), true);
                drawSearchResults();
                ImGui::EndChild();
            }
            else
            {
                float leftPanelWidth = 160.0f;
                float contentHeight = -ImGui::GetFrameHeightWithSpacing() - 8.0f;

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
    }
}
